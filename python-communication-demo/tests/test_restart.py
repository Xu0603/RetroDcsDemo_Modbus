#!/usr/bin/env python3
from pathlib import Path
import os
import re
import signal
import subprocess
import sys
import time


def wait_for_pids(log_path: Path, count: int, timeout: float) -> list[int]:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if log_path.exists():
            text = log_path.read_text(encoding="utf-8")
            pids = [int(v) for v in re.findall(r"event=runner_started detail=task=1 pid=(\d+)", text)]
            if len(pids) >= count:
                return pids
        time.sleep(0.1)
    raise TimeoutError(f"did not observe {count} runner starts")


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    run = root / "run" / "restart-test"
    run.mkdir(parents=True, exist_ok=True)
    log = run / "runtime.log"
    snapshot = run / "snapshot.json"
    for path in (log, snapshot):
        path.unlink(missing_ok=True)
    subprocess.run([sys.executable, str(root / "tools" / "create_demo_db.py")], check=True)
    command = [
        str(root / "build" / "system-managerd"),
        "--database", str(root / "database" / "demo.sqlite"),
        "--scripts-root", str(root / "scripts" / "examples"),
        "--shm-name", f"/pycom_restart_{os.getpid()}",
        "--log", str(log),
        "--python", sys.executable,
        "--run-seconds", "9",
        "--snapshot-out", str(snapshot),
    ]
    manager = subprocess.Popen(command)
    try:
        first = wait_for_pids(log, 1, 4.0)[0]
        os.kill(first, signal.SIGKILL)
        pids = wait_for_pids(log, 2, 6.0)
        if pids[0] == pids[1]:
            raise RuntimeError("runner pid did not change")
        print(f"restart observed: {pids[0]} -> {pids[1]}")
        return_code = manager.wait(timeout=12)
        if return_code != 0:
            raise RuntimeError(f"manager exited with {return_code}")
        text = log.read_text(encoding="utf-8")
        if "event=runner_exited" not in text:
            raise RuntimeError("runner exit was not recorded")
        if not snapshot.exists() or '"value":15.480000' not in snapshot.read_text(encoding="utf-8"):
            raise RuntimeError("calculated value not restored after restart")
        return 0
    finally:
        if manager.poll() is None:
            manager.terminate()
            manager.wait(timeout=3)


if __name__ == "__main__":
    raise SystemExit(main())
