from __future__ import annotations

import urllib.parse


def encode_message(message: dict[str, object]) -> bytes:
    lines = []
    for key, value in sorted(message.items()):
        k = urllib.parse.quote(str(key), safe="_-./ ")
        v = urllib.parse.quote(str(value), safe="_-./ ")
        lines.append(f"{k}={v}\n")
    return "".join(lines).encode("utf-8")


def decode_message(payload: bytes) -> dict[str, str]:
    result: dict[str, str] = {}
    for raw_line in payload.decode("utf-8").splitlines():
        if not raw_line:
            continue
        if "=" not in raw_line:
            raise ValueError("malformed IPC field")
        key, value = raw_line.split("=", 1)
        result[urllib.parse.unquote(key)] = urllib.parse.unquote(value)
    return result
