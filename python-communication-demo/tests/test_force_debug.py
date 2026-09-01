#!/usr/bin/env python3
from pathlib import Path
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time


def manager_request(message_type: int, payload: dict, request_id: int) -> dict:
    body = json.dumps(payload).encode()
    frame = struct.pack("!4sHHII", b"PYSM", 1, message_type, request_id, len(body)) + body
    with socket.create_connection(("127.0.0.1", 19090), timeout=1) as connection:
        connection.sendall(frame)
        header = b""
        while len(header) < 16:
            header += connection.recv(16 - len(header))
        magic, version, response_type, response_id, length = struct.unpack("!4sHHII", header)
        assert (magic, version, response_type, response_id) == (b"PYSM", 1, message_type | 0x8000, request_id)
        response = b""
        while len(response) < length:
            response += connection.recv(length - len(response))
        return json.loads(response)


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="pycom-force-") as temporary:
        database = Path(temporary) / "config.sqlite"
        subprocess.run([sys.executable, str(root / "tools" / "create_demo_db.py"), str(database)], check=True)
        manager = subprocess.Popen([
            str(root / "build" / "system-managerd"),
            "--database", str(database),
            "--scripts-root", str(root / "scripts" / "examples"),
            "--shm-name", f"/pycom_force_{os.getpid()}",
            "--log", str(Path(temporary) / "runtime.log"),
            "--python", sys.executable,
        ])
        try:
            deadline = time.monotonic() + 4
            while True:
                try:
                    ping = manager_request(1, {}, 1)
                    break
                except OSError:
                    if time.monotonic() >= deadline:
                        raise
                    time.sleep(0.05)
            forced = manager_request(3, {
                "pointId": 1, "value": "31.5", "configHash": ping["configHash"]
            }, 2)
            if not forced.get("ok"):
                raise RuntimeError(f"force failed: {forced}")
            snapshot = manager_request(2, {}, 3)
            temperature = next(point for point in snapshot["points"] if point["id"] == 1)
            if abs(float(temperature["value"]) - 31.5) > 1e-9:
                raise RuntimeError(f"forced value not visible: {temperature}")

            deadline = time.monotonic() + 4
            output = []
            while time.monotonic() < deadline:
                try:
                    with socket.create_connection(("127.0.0.1", 20001), timeout=1) as debug:
                        debug.sendall(b"GET 0\n")
                        data = b""
                        while True:
                            chunk = debug.recv(4096)
                            if not chunk:
                                break
                            data += chunk
                    output = [json.loads(line) for line in data.splitlines()]
                    if any(entry.get("stream") == "stdout" and "result=" in entry.get("text", "") for entry in output):
                        break
                except OSError:
                    pass
                time.sleep(0.1)
            else:
                raise RuntimeError(f"Python print output not received: {output}")
            print("force write and Python debug TCP tests passed")
            return 0
        finally:
            if manager.poll() is None:
                manager.terminate()
            manager.wait(timeout=4)


if __name__ == "__main__":
    raise SystemExit(main())
