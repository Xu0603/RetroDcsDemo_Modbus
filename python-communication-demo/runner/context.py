from __future__ import annotations

from dataclasses import dataclass
import itertools
import time
from typing import Iterable


@dataclass(frozen=True)
class PointValue:
    value: bool | int | float
    data_type: str
    status: str
    timestamp_ns: int
    sequence: int


class ScriptContext:
    def __init__(self, client: "RunnerClient") -> None:
        self._client = client

    @property
    def task_id(self) -> int:
        return self._client.task_id

    @property
    def config_version(self) -> int:
        return self._client.config_version

    def read(self, alias: str) -> PointValue:
        response = self._client.request("READ_REQUEST", alias=alias)
        self._require_ok(response)
        return PointValue(
            value=_parse_typed_value(response["data_type"], response["value"]),
            data_type=response["data_type"],
            status=response["status"],
            timestamp_ns=int(response["timestamp_ns"]),
            sequence=int(response["sequence"]),
        )

    def read_many(self, aliases: Iterable[str]) -> dict[str, PointValue]:
        return {alias: self.read(alias) for alias in aliases}

    def write(self, alias: str, value: bool | int | float) -> None:
        response = self._client.request("WRITE_REQUEST", alias=alias, value=_format_value(value))
        self._require_ok(response)

    def write_many(self, values: dict[str, bool | int | float]) -> None:
        for alias, value in values.items():
            self.write(alias, value)

    def subscribe(self, aliases: Iterable[str]) -> None:
        response = self._client.request("SUBSCRIBE_REQUEST", aliases=",".join(aliases))
        self._require_ok(response)

    def log(self, level: str, message: str) -> None:
        print(f"[{level.upper()}] {message}")
        self._client.send_event("SCRIPT_LOG", level=level, message=message)

    @staticmethod
    def _require_ok(response: dict[str, str]) -> None:
        if response.get("code") != "OK":
            raise RuntimeError(f"manager rejected request: {response.get('code')} {response.get('message', '')}")


def _parse_typed_value(data_type: str, value: str) -> bool | int | float:
    if data_type == "BOOL":
        return value.lower() in ("true", "1")
    if data_type in ("FLOAT32", "FLOAT64"):
        return float(value)
    return int(value)


def _format_value(value: bool | int | float) -> str:
    if type(value) is bool:
        return "true" if value else "false"
    if type(value) in (int, float):
        return str(value)
    raise TypeError(f"unsupported script value type: {type(value).__name__}")


class RunnerClient:
    """Protocol-facing API implemented by runner.py."""

    task_id: int
    config_version: int

    def request(self, message_type: str, **fields: object) -> dict[str, str]:
        raise NotImplementedError

    def send_event(self, message_type: str, **fields: object) -> None:
        raise NotImplementedError
