#!/usr/bin/env python3
from pathlib import Path
import sqlite3
import sys


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    output = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "database" / "demo.sqlite"
    output.parent.mkdir(parents=True, exist_ok=True)
    # 上一次 WAL 模式运行可能留下辅助文件；重建主库时必须一起清理。
    for database_file in (output, Path(str(output) + "-wal"), Path(str(output) + "-shm")):
        database_file.unlink(missing_ok=True)
    connection = sqlite3.connect(output)
    try:
        connection.executescript((root / "database" / "schema.sql").read_text(encoding="utf-8"))
        connection.executescript((root / "database" / "seed.sql").read_text(encoding="utf-8"))
        connection.commit()
        # system-managerd 以只读方式打开已发布配置；交付文件切回单文件日志模式，
        # 避免首次只读打开时还需要创建 WAL/SHM 辅助文件。
        connection.execute("PRAGMA wal_checkpoint(TRUNCATE)")
        connection.execute("PRAGMA journal_mode=DELETE")
    finally:
        connection.close()
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
