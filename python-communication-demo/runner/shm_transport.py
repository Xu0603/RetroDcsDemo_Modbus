from __future__ import annotations

import ctypes
import errno
import os
import time

from protocol import decode_message, encode_message


ABI_VERSION = 1
CAPACITY = 64
SLOT_PAYLOAD = 4092


class QueueMeta(ctypes.Structure):
    _fields_ = [("read_index", ctypes.c_uint32), ("write_index", ctypes.c_uint32)]


class Slot(ctypes.Structure):
    _fields_ = [("length", ctypes.c_uint32), ("payload", ctypes.c_char * SLOT_PAYLOAD)]


class SharedLayout(ctypes.Structure):
    _fields_ = [
        ("magic", ctypes.c_char * 8),
        ("abi_version", ctypes.c_uint32),
        ("layout_size", ctypes.c_uint32),
        ("capacity", ctypes.c_uint32),
        ("slot_payload", ctypes.c_uint32),
        ("manager_pid", ctypes.c_int64),
        ("runner_pid", ctypes.c_int64),
        ("manager_heartbeat_ns", ctypes.c_int64),
        ("runner_heartbeat_ns", ctypes.c_int64),
        ("process_epoch", ctypes.c_char * 64),
        ("requests", QueueMeta),
        ("responses", QueueMeta),
        ("request_slots", Slot * CAPACITY),
        ("response_slots", Slot * CAPACITY),
    ]


class PosixShmTransport:
    def __init__(self, name: str) -> None:
        self.name = name if name.startswith("/") else "/" + name
        self.libc = ctypes.CDLL(None, use_errno=True)
        self.libc.shm_open.restype = ctypes.c_int
        self.libc.mmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int,
                                   ctypes.c_int, ctypes.c_int, ctypes.c_long]
        self.libc.mmap.restype = ctypes.c_void_p
        self.libc.munmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        self.libc.sem_open.restype = ctypes.c_void_p
        self.libc.sem_trywait.argtypes = [ctypes.c_void_p]
        self.libc.sem_post.argtypes = [ctypes.c_void_p]
        self.libc.sem_close.argtypes = [ctypes.c_void_p]
        self.fd = -1
        self.address: int | None = None
        self.layout: SharedLayout | None = None
        self.semaphores: list[int] = []

    def open(self, expected_epoch: str) -> None:
        deadline = time.monotonic() + 10.0
        while True:
            self.fd = self.libc.shm_open(self.name.encode(), os.O_RDWR, 0)
            if self.fd >= 0:
                break
            if ctypes.get_errno() != errno.ENOENT or time.monotonic() >= deadline:
                raise OSError(ctypes.get_errno(), f"shm_open failed for {self.name}")
            time.sleep(0.05)
        size = ctypes.sizeof(SharedLayout)
        address = self.libc.mmap(None, size, 0x1 | 0x2, 0x01, self.fd, 0)
        if address == ctypes.c_void_p(-1).value:
            raise OSError(ctypes.get_errno(), "mmap failed")
        self.address = address
        self.layout = SharedLayout.from_address(address)
        if bytes(self.layout.magic) != b"PYCOMIPC":
            raise RuntimeError("IPC shared-memory magic mismatch")
        if self.layout.abi_version != ABI_VERSION or self.layout.layout_size != size:
            raise RuntimeError("IPC shared-memory ABI mismatch")
        epoch = bytes(self.layout.process_epoch).split(b"\0", 1)[0].decode()
        if epoch != expected_epoch:
            raise RuntimeError("IPC process epoch mismatch")
        self.layout.runner_pid = os.getpid()
        self.layout.runner_heartbeat_ns = time.time_ns()
        self.request_slots = self._open_sem("_qs")
        self.request_items = self._open_sem("_qi")
        self.request_mutex = self._open_sem("_qm")
        self.response_slots = self._open_sem("_ps")
        self.response_items = self._open_sem("_pi")
        self.response_mutex = self._open_sem("_pm")

    def _open_sem(self, suffix: str) -> int:
        pointer = self.libc.sem_open((self.name + suffix).encode(), 0)
        if pointer == ctypes.c_void_p(-1).value:
            raise OSError(ctypes.get_errno(), f"sem_open failed for {self.name + suffix}")
        self.semaphores.append(pointer)
        return pointer

    def _wait(self, semaphore: int, timeout: float) -> bool:
        deadline = time.monotonic() + timeout
        while True:
            if self.libc.sem_trywait(semaphore) == 0:
                return True
            error = ctypes.get_errno()
            if error not in (errno.EAGAIN, errno.EINTR):
                raise OSError(error, "sem_trywait failed")
            if time.monotonic() >= deadline:
                return False
            time.sleep(0.001)

    def _push(self, meta: QueueMeta, slots, slots_sem: int, items_sem: int,
              mutex_sem: int, payload: bytes, timeout: float) -> None:
        if not payload or len(payload) > SLOT_PAYLOAD:
            raise ValueError("IPC message exceeds fixed shared-memory slot")
        if not self._wait(slots_sem, timeout):
            raise TimeoutError("IPC queue has no free slot")
        if not self._wait(mutex_sem, timeout):
            self.libc.sem_post(slots_sem)
            raise TimeoutError("IPC queue mutex timeout")
        try:
            slot = slots[meta.write_index % CAPACITY]
            slot.length = len(payload)
            ctypes.memmove(ctypes.addressof(slot) + Slot.payload.offset, payload, len(payload))
            meta.write_index = (meta.write_index + 1) % CAPACITY
        finally:
            self.libc.sem_post(mutex_sem)
        self.libc.sem_post(items_sem)

    def _pop(self, meta: QueueMeta, slots, slots_sem: int, items_sem: int,
             mutex_sem: int, timeout: float) -> bytes | None:
        if not self._wait(items_sem, timeout):
            return None
        if not self._wait(mutex_sem, timeout):
            self.libc.sem_post(items_sem)
            raise TimeoutError("IPC queue mutex timeout")
        try:
            slot = slots[meta.read_index % CAPACITY]
            if slot.length <= 0 or slot.length > SLOT_PAYLOAD:
                raise RuntimeError("invalid IPC shared-memory slot length")
            payload = ctypes.string_at(ctypes.addressof(slot) + Slot.payload.offset, slot.length)
            slot.length = 0
            meta.read_index = (meta.read_index + 1) % CAPACITY
        finally:
            self.libc.sem_post(mutex_sem)
            self.libc.sem_post(slots_sem)
        return payload

    def send(self, message: dict[str, object], timeout: float = 1.0) -> None:
        assert self.layout is not None
        self._push(self.layout.requests, self.layout.request_slots, self.request_slots,
                   self.request_items, self.request_mutex, encode_message(message), timeout)

    def receive(self, timeout: float = 1.0) -> dict[str, str] | None:
        assert self.layout is not None
        payload = self._pop(self.layout.responses, self.layout.response_slots,
                            self.response_slots, self.response_items,
                            self.response_mutex, timeout)
        return decode_message(payload) if payload is not None else None

    def heartbeat(self) -> None:
        assert self.layout is not None
        self.layout.runner_heartbeat_ns = time.time_ns()

    def close(self) -> None:
        for semaphore in self.semaphores:
            self.libc.sem_close(semaphore)
        self.semaphores.clear()
        if self.address is not None:
            self.libc.munmap(ctypes.c_void_p(self.address), ctypes.sizeof(SharedLayout))
            self.address = None
        if self.fd >= 0:
            os.close(self.fd)
            self.fd = -1
