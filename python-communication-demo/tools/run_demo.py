#!/usr/bin/env python3
from pathlib import Path
import os
import subprocess
import sys


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    build = root / "build"
    database = root / "database" / "demo.sqlite"
    subprocess.run([sys.executable, str(root / "tools" / "create_demo_db.py"), str(database)], check=True)
    snapshot = root / "run" / "snapshot.json"
    log = root / "run" / "runtime.log"
    snapshot.parent.mkdir(exist_ok=True)
    command = [
        str(build / "system-managerd"),
        "--database", str(database),
        "--scripts-root", str(root / "scripts" / "examples"),
        "--shm-name", f"/pycom_demo_{os.getpid()}",
        "--log", str(log),
        "--python", sys.executable,
        "--run-seconds", "6",
        "--snapshot-out", str(snapshot),
    ]
    print(" ".join(command))
    result = subprocess.run(command, check=False)
    if snapshot.exists():
        print(snapshot.read_text(encoding="utf-8"))
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
