from __future__ import annotations

from collections import deque
import json
import socketserver
import sys
import threading
import time
from typing import TextIO


class DebugBuffer:
    def __init__(self, capacity: int = 2000) -> None:
        self._entries: deque[dict[str, object]] = deque(maxlen=capacity)
        self._sequence = 0
        self._lock = threading.Lock()

    def append(self, stream: str, text: str) -> None:
        text = text.rstrip("\r\n")
        if not text:
            return
        with self._lock:
            self._sequence += 1
            self._entries.append({
                "sequence": self._sequence,
                "timestampNs": time.time_ns(),
                "stream": stream,
                "text": text,
            })

    def after(self, cursor: int) -> tuple[list[dict[str, object]], int]:
        with self._lock:
            if cursor > self._sequence:  # Runner restarted and its sequence restarted from zero.
                cursor = 0
            entries = [dict(entry) for entry in self._entries if int(entry["sequence"]) > cursor]
            return entries, self._sequence


class CapturedStream:
    def __init__(self, original: TextIO, stream_name: str, buffer: DebugBuffer) -> None:
        self._original = original
        self._stream_name = stream_name
        self._buffer = buffer
        self._pending = ""
        self._lock = threading.Lock()

    def write(self, text: str) -> int:
        self._original.write(text)
        self._original.flush()
        with self._lock:
            self._pending += text
            while "\n" in self._pending:
                line, self._pending = self._pending.split("\n", 1)
                self._buffer.append(self._stream_name, line)
        return len(text)

    def flush(self) -> None:
        self._original.flush()
        with self._lock:
            if self._pending:
                self._buffer.append(self._stream_name, self._pending)
                self._pending = ""

    def isatty(self) -> bool:
        return False

    @property
    def encoding(self) -> str:
        return getattr(self._original, "encoding", "utf-8") or "utf-8"


class _DebugTcpServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


class RunnerDebugServer:
    def __init__(self, port: int) -> None:
        self.port = port
        self.buffer = DebugBuffer()
        self._server: _DebugTcpServer | None = None
        self._thread: threading.Thread | None = None
        self._stdout = sys.stdout
        self._stderr = sys.stderr

    def start(self) -> None:
        buffer = self.buffer

        class Handler(socketserver.StreamRequestHandler):
            def handle(self) -> None:
                command = self.rfile.readline(256).decode("utf-8", errors="replace").strip().split()
                if not command or command[0] != "GET":
                    return
                try:
                    cursor = int(command[1]) if len(command) > 1 else 0
                except ValueError:
                    cursor = 0
                entries, latest = buffer.after(max(0, cursor))
                for entry in entries:
                    self.wfile.write(json.dumps(entry, ensure_ascii=False).encode("utf-8") + b"\n")
                self.wfile.write(json.dumps({"type": "END", "cursor": latest}).encode("utf-8") + b"\n")

        self._server = _DebugTcpServer(("127.0.0.1", self.port), Handler)
        self._thread = threading.Thread(target=self._server.serve_forever,
                                        name=f"python-debug-{self.port}", daemon=True)
        self._thread.start()
        sys.stdout = CapturedStream(self._stdout, "stdout", self.buffer)
        sys.stderr = CapturedStream(self._stderr, "stderr", self.buffer)
        self.buffer.append("system", f"Runner 调试服务已启动：127.0.0.1:{self.port}")

    def stop(self) -> None:
        self.buffer.append("system", "Runner 正在停止")
        if isinstance(sys.stdout, CapturedStream):
            sys.stdout.flush()
        if isinstance(sys.stderr, CapturedStream):
            sys.stderr.flush()
        sys.stdout = self._stdout
        sys.stderr = self._stderr
        if self._server:
            self._server.shutdown()
            self._server.server_close()
        if self._thread:
            self._thread.join(timeout=1.0)
        self._server = None
        self._thread = None
