from __future__ import annotations

import argparse
import importlib.util
import os
from pathlib import Path
import signal
import sys
import time
import traceback
import uuid

from context import RunnerClient, ScriptContext
from debug_server import RunnerDebugServer
from shm_transport import PosixShmTransport


PROTOCOL_VERSION = 1


class ExecutionTimeout(Exception):
    pass


def _timeout_handler(_signum, _frame):
    raise ExecutionTimeout("script callback exceeded configured timeout")


class Client(RunnerClient):
    def __init__(self, ipc_name: str, task_id: int, process_epoch: str) -> None:
        self.transport = PosixShmTransport(ipc_name)
        self.task_id = task_id
        self.process_epoch = process_epoch
        self.config_version = 0
        self.request_no = 0
        self.last_heartbeat = 0.0
        self.pending_changes: list[dict[str, str]] = []
        self.manual_requested = False

    def connect(self) -> dict[str, str]:
        self.transport.open(self.process_epoch)
        response = self.request("HELLO", protocol_version=PROTOCOL_VERSION, pid=os.getpid())
        if response.get("code") != "OK":
            raise RuntimeError(f"HELLO rejected: {response}")
        self.config_version = int(response["config_version"])
        config = self.transport.receive(5.0)
        if not config or config.get("type") != "TASK_CONFIG":
            raise RuntimeError("manager did not send TASK_CONFIG")
        return config

    def _base(self, message_type: str, request_id: str) -> dict[str, object]:
        return {
            "type": message_type,
            "request_id": request_id,
            "task_id": self.task_id,
            "protocol_version": PROTOCOL_VERSION,
            "process_epoch": self.process_epoch,
        }

    def request(self, message_type: str, **fields: object) -> dict[str, str]:
        self.request_no += 1
        request_id = f"{self.process_epoch}-{self.request_no}"
        message = self._base(message_type, request_id)
        message.update(fields)
        self.transport.send(message)
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            response = self.transport.receive(max(0.01, deadline - time.monotonic()))
            if response is None:
                continue
            if response.get("request_id") == request_id:
                return response
            self._handle_unsolicited(response)
        raise TimeoutError(f"manager response timeout for {message_type}")

    def send_event(self, message_type: str, **fields: object) -> None:
        request_id = f"event-{uuid.uuid4().hex}"
        message = self._base(message_type, request_id)
        message.update(fields)
        self.transport.send(message)

    def heartbeat(self) -> None:
        now = time.monotonic()
        if now - self.last_heartbeat >= 1.0:
            self.send_event("HEARTBEAT", monotonic_ns=time.monotonic_ns())
            self.transport.heartbeat()
            self.last_heartbeat = now

    def _handle_unsolicited(self, message: dict[str, str]) -> None:
        if message.get("type") == "VALUE_UPDATE":
            self.pending_changes.append(message)
            return
        if message.get("type") == "MANUAL_TRIGGER":
            self.manual_requested = True
            return
        if message.get("type") == "HEARTBEAT_ACK":
            return
        if message.get("type") in ("TASK_STOP", "SHUTDOWN"):
            raise KeyboardInterrupt


def load_script(path: Path):
    spec = importlib.util.spec_from_file_location(f"user_task_{os.getpid()}", path)
    if not spec or not spec.loader:
        raise RuntimeError(f"cannot load script: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def invoke(module, callback_name: str, ctx: ScriptContext, timeout_ms: int, *extra_args) -> None:
    callback = getattr(module, callback_name, None)
    if callback is None:
        return
    previous = signal.signal(signal.SIGALRM, _timeout_handler)
    signal.setitimer(signal.ITIMER_REAL, timeout_ms / 1000.0)
    started = time.monotonic_ns()
    try:
        callback(ctx, *extra_args)
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, previous)
        elapsed = time.monotonic_ns() - started
        ctx._client.send_event("EXECUTION_STAT", callback=callback_name, elapsed_ns=elapsed)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ipc-name", required=True)
    parser.add_argument("--task-id", type=int, required=True)
    parser.add_argument("--epoch", required=True)
    parser.add_argument("--scripts-root", required=True)
    parser.add_argument("--debug-port", type=int, required=True)
    args = parser.parse_args()

    client = Client(args.ipc_name, args.task_id, args.epoch)
    debug_server = RunnerDebugServer(args.debug_port)
    debug_server.start()
    module = None
    ctx = ScriptContext(client)
    try:
        config = client.connect()
        print(f"[RUNNER] 任务 {args.task_id} 已连接 system-managerd，配置版本 {client.config_version}")
        script_path = (Path(args.scripts_root).resolve() / config["script_path"]).resolve()
        root = Path(args.scripts_root).resolve()
        if root not in script_path.parents:
            raise RuntimeError("script path escapes scripts root")
        module = load_script(script_path)
        print(f"[RUNNER] 已加载脚本 {script_path.name}")
        interval = int(config["interval_ms"]) / 1000.0
        timeout_ms = int(config["timeout_ms"])
        invoke(module, "on_start", ctx, timeout_ms)
        if config["trigger_type"] == "POINT_CHANGE":
            read_aliases = []
            for raw_binding in config.get("bindings", "").split(";"):
                if not raw_binding:
                    continue
                alias, _point_id, _data_type, access_mode = raw_binding.split("|", 3)
                if access_mode in ("READ", "READ_WRITE"):
                    read_aliases.append(alias)
            ctx.subscribe(read_aliases)
        next_run = time.monotonic()
        while True:
            client.heartbeat()
            now = time.monotonic()
            if config["trigger_type"] == "PERIODIC" and now >= next_run:
                invoke(module, config["entry_function"], ctx, timeout_ms)
                next_run = max(next_run + interval, time.monotonic())
            elif config["trigger_type"] == "POINT_CHANGE" and client.pending_changes:
                changes = client.pending_changes[:]
                client.pending_changes.clear()
                invoke(module, config["entry_function"], ctx, timeout_ms, changes)
            elif config["trigger_type"] == "MANUAL" and client.manual_requested:
                client.manual_requested = False
                invoke(module, config["entry_function"], ctx, timeout_ms)
            message = client.transport.receive(min(0.1, max(0.0, next_run - time.monotonic())))
            if message:
                client._handle_unsolicited(message)
    except KeyboardInterrupt:
        if module:
            try:
                invoke(module, "on_stop", ctx, 500)
            except Exception:
                pass
        return 0
    except Exception as exc:
        try:
            client.send_event("SCRIPT_EXCEPTION", error=type(exc).__name__, message=str(exc),
                              traceback=traceback.format_exc())
        except Exception:
            pass
        print(traceback.format_exc(), file=sys.stderr)
        return 2
    finally:
        client.transport.close()
        debug_server.stop()


if __name__ == "__main__":
    raise SystemExit(main())
