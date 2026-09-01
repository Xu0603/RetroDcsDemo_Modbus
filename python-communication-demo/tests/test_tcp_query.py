#!/usr/bin/env python3

from pathlib import Path
import json
import os
import socket
import struct
import subprocess
import sys
import time


def request(message_type: int, request_id: int = 1) -> dict:
    payload = b"{}"

    frame = struct.pack(
        "!4sHHII",
        b"PYSM",
        1,
        message_type,
        request_id,
        len(payload),
    ) + payload

    with socket.create_connection(
        ("127.0.0.1", 19090),
        timeout=1.0,
    ) as connection:

        connection.sendall(frame)

        header = connection.recv(16)

        if len(header) != 16:
            raise RuntimeError("short TCP response header")

        magic, version, response_type, response_id, length = struct.unpack(
            "!4sHHII",
            header,
        )

        if magic != b"PYSM":
            raise RuntimeError("invalid TCP magic")

        if version != 1:
            raise RuntimeError("invalid TCP protocol version")

        if response_id != request_id:
            raise RuntimeError("invalid TCP response id")

        body = b""

        while len(body) < length:

            chunk = connection.recv(length - len(body))

            if not chunk:
                raise RuntimeError("short TCP response body")

            body += chunk

        return json.loads(body)


def main() -> int:

    root = Path(__file__).resolve().parents[1]

    subprocess.run(
        [
            sys.executable,
            str(root / "tools" / "create_demo_db.py"),
        ],
        check=True,
    )

    manager = subprocess.Popen(
        [
            str(root / "build" / "system-managerd"),

            "--database",
            str(root / "database" / "demo.sqlite"),

            "--scripts-root",
            str(root / "scripts" / "examples"),

            "--shm-name",
            f"/pycom_tcp_{os.getpid()}",

            "--log",
            str(root / "run" / "tcp-test.log"),

            "--python",
            sys.executable,

            "--run-seconds",
            "15",
        ]
    )

    try:

        # 等待 system-managerd TCP 服务启动
        deadline = time.monotonic() + 5.0

        while True:

            try:
                ping = request(1)
                break

            except OSError:

                if time.monotonic() >= deadline:
                    raise

                time.sleep(0.05)

        # 检查 PING
        if not ping.get("ok"):
            raise RuntimeError(
                f"unexpected PING response: {ping}"
            )

        print("PING test passed")

        # 等待 Modbus 采集数据
        time.sleep(7.0)

        # 获取实时快照
        snapshot = request(2, 2)

        if not snapshot.get("ok"):
            raise RuntimeError(
                f"snapshot failed: {snapshot}"
            )

        points = {
            point["name"]: point
            for point in snapshot["points"]
        }

        print("Snapshot points:")

        for name, point in points.items():
            print(
                f"  {name}: "
                f"value={point['value']} "
                f"status={point['status']}"
            )

        # =====================================================
        # 检查基础 Python 通信点仍然存在
        # =====================================================

        basic_points = {
            "temperature",
            "pressure",
            "calculated_value",
        }

        if not basic_points.issubset(points.keys()):

            raise RuntimeError(
                "missing basic points: "
                f"expected={basic_points}, "
                f"actual={set(points.keys())}"
            )

        print("Basic point test passed")

        # =====================================================
        # 检查新增 Modbus 点
        # =====================================================

        modbus_points = {
            "modbus_uint16",
            "modbus_int16",
            "modbus_uint32",
            "modbus_int32",
            "modbus_float32",
            "modbus_float64",
        }

        if not modbus_points.issubset(points.keys()):

            raise RuntimeError(
                "missing Modbus points: "
                f"expected={modbus_points}, "
                f"actual={set(points.keys())}"
            )

        print("Modbus point existence test passed")

        # =====================================================
        # 检查 Modbus 点状态
        # =====================================================

        for point_name in modbus_points:

            point = points[point_name]

            if point["status"] != "Good":

                raise RuntimeError(
                    f"Modbus point {point_name} "
                    f"status is not Good: "
                    f"{point}"
                )

        print("Modbus status test passed")

        # =====================================================
        # 检查几个关键 Modbus 数值
        # =====================================================

        if points["modbus_uint16"]["value"] != "12345":

            raise RuntimeError(
                "modbus_uint16 value mismatch: "
                f"{points['modbus_uint16']}"
            )

        if points["modbus_int16"]["value"] != "-12345":

            raise RuntimeError(
                "modbus_int16 value mismatch: "
                f"{points['modbus_int16']}"
            )

        if points["modbus_uint32"]["value"] != "123456789":

            raise RuntimeError(
                "modbus_uint32 value mismatch: "
                f"{points['modbus_uint32']}"
            )

        if points["modbus_int32"]["value"] != "-123456789":

            raise RuntimeError(
                "modbus_int32 value mismatch: "
                f"{points['modbus_int32']}"
            )

        print("Modbus integer value test passed")

        # FLOAT 使用浮点比较
        float32_value = float(
            points["modbus_float32"]["value"]
        )

        if abs(float32_value - 1.0) > 1e-6:

            raise RuntimeError(
                "modbus_float32 value mismatch: "
                f"{points['modbus_float32']}"
            )

        float64_value = float(
            points["modbus_float64"]["value"]
        )

        if abs(float64_value - 1.0) > 1e-6:

            raise RuntimeError(
                "modbus_float64 value mismatch: "
                f"{points['modbus_float64']}"
            )

        print("Modbus floating-point value test passed")

        print()
        print("======================================")
        print("TCP query test passed")
        print("Modbus integration test passed")
        print("======================================")

        return 0

    finally:

        if manager.poll() is None:

            manager.terminate()

        try:
            manager.wait(timeout=3)

        except subprocess.TimeoutExpired:

            manager.kill()

            manager.wait(timeout=3)


if __name__ == "__main__":
    raise SystemExit(main())