from __future__ import annotations

import base64
import json
import secrets
import socket
import struct
from typing import Any, Dict, Iterable, Optional

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 6767
_MAX_TXN_ID = 2**63 - 1
_LENGTH_PREFIX_FORMAT = "!I"


class ProtocolError(RuntimeError):
    """Raised when a wire-format or framing violation is encountered."""


def connect(
    host: str = DEFAULT_HOST, port: int = DEFAULT_PORT, timeout: Optional[float] = 5.0
) -> socket.socket:
    """Open a TCP connection to a Jubilant DB server."""
    sock = socket.create_connection((host, port), timeout=timeout)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return sock


def send_transaction(
    sock: socket.socket, txn_id: int, operations: Iterable[Dict[str, Any]]
) -> Dict[str, Any]:
    """Send a transaction request and return the parsed JSON response."""
    _validate_txn_id(txn_id)

    normalized_ops = [_normalize_operation(op) for op in operations]
    if not normalized_ops:
        raise ValueError("operations list must be non-empty")

    request = {"txn_id": txn_id, "operations": normalized_ops}
    _send_frame(sock, request)
    response = _recv_frame(sock)

    if not isinstance(response, dict):
        raise ProtocolError("response payload must be a JSON object")
    if response.get("txn_id") != txn_id:
        raise ProtocolError("response txn_id does not match request")

    return response


def set_value(
    sock: socket.socket,
    key: str,
    value: Any,
    *,
    kind: str = "string",
    metadata: Optional[Dict[str, Any]] = None,
    txn_id: Optional[int] = None,
) -> Dict[str, Any]:
    """Execute a single-key set transaction."""
    return send_transaction(
        sock,
        txn_id if txn_id is not None else _next_txn_id(),
        [{"type": "set", "key": key, "value": _encode_value(kind, value, metadata)}],
    )


def get_value(
    sock: socket.socket, key: str, *, txn_id: Optional[int] = None
) -> Dict[str, Any]:
    """Execute a single-key get transaction."""
    return send_transaction(
        sock, txn_id if txn_id is not None else _next_txn_id(), [{"type": "get", "key": key}]
    )


def delete_value(
    sock: socket.socket, key: str, *, txn_id: Optional[int] = None
) -> Dict[str, Any]:
    """Execute a single-key delete transaction."""
    return send_transaction(
        sock, txn_id if txn_id is not None else _next_txn_id(), [{"type": "del", "key": key}]
    )


def _next_txn_id() -> int:
    return secrets.randbits(63)


def _validate_txn_id(txn_id: int) -> None:
    if not isinstance(txn_id, int):
        raise ValueError("txn_id must be an integer")
    if txn_id < 0 or txn_id > _MAX_TXN_ID:
        raise ValueError(f"txn_id must be within 0..{_MAX_TXN_ID}")


def _validate_key(key: str) -> None:
    if not isinstance(key, str) or not key:
        raise ValueError("key must be a non-empty string")


def _encode_value(kind: str, value: Any, metadata: Optional[Dict[str, Any]]) -> Dict[str, Any]:
    if kind == "bytes":
        if isinstance(value, str):
            raise ValueError("bytes values must be provided as bytes-like objects")
        data_bytes = bytes(value)
        encoded = base64.b64encode(data_bytes).decode("ascii")
    elif kind == "string":
        if not isinstance(value, str):
            raise ValueError("string values must be provided as str")
        encoded = value
    elif kind == "int":
        if not isinstance(value, int):
            raise ValueError("int values must be provided as int")
        if value < -2**63 or value > 2**63 - 1:
            raise ValueError("int values must fit within signed 64-bit range")
        encoded = value
    else:
        raise ValueError("kind must be one of 'bytes', 'string', or 'int'")

    result: Dict[str, Any] = {"kind": kind, "data": encoded}
    if metadata:
        result["metadata"] = metadata
    return result


def _normalize_operation(op: Dict[str, Any]) -> Dict[str, Any]:
    if not isinstance(op, dict):
        raise ValueError("operation entries must be dictionaries")

    op_type = op.get("type")
    key = op.get("key")
    _validate_key(key)

    normalized: Dict[str, Any] = {"type": op_type, "key": key}

    if op_type == "set":
        if "value" not in op:
            raise ValueError("set operations must include a value")
        normalized["value"] = _normalize_value(op["value"])
    elif op_type in {"get", "del"}:
        normalized.pop("value", None)
    else:
        raise ValueError("operation type must be one of 'get', 'set', or 'del'")

    for extra_key, extra_value in op.items():
        if extra_key not in {"type", "key", "value"}:
            normalized[extra_key] = extra_value

    return normalized


def _normalize_value(value: Any) -> Dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError("value must be a mapping with kind/data")

    if "kind" not in value or "data" not in value:
        raise ValueError("value must include 'kind' and 'data'")

    kind = value["kind"]
    data = value["data"]

    if kind == "bytes":
        if not isinstance(data, str):
            raise ValueError("bytes data must be base64-encoded string")
    elif kind == "string":
        if not isinstance(data, str):
            raise ValueError("string data must be a string")
    elif kind == "int":
        if not isinstance(data, int):
            raise ValueError("int data must be an integer")
        if data < -2**63 or data > 2**63 - 1:
            raise ValueError("int data must fit within signed 64-bit range")
    else:
        raise ValueError("kind must be one of 'bytes', 'string', or 'int'")

    normalized = {"kind": kind, "data": data}
    if "metadata" in value:
        normalized["metadata"] = value["metadata"]
    return normalized


def _send_frame(sock: socket.socket, payload: Dict[str, Any]) -> None:
    body = json.dumps(payload, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    if len(body) == 0:
        raise ValueError("payload must not be empty")
    if len(body) >= 2**32:
        raise ValueError("payload exceeds maximum frame size")

    header = struct.pack(_LENGTH_PREFIX_FORMAT, len(body))
    sock.sendall(header + body)


def _recv_frame(sock: socket.socket) -> Any:
    length_prefix = _read_exact(sock, struct.calcsize(_LENGTH_PREFIX_FORMAT))
    if len(length_prefix) != struct.calcsize(_LENGTH_PREFIX_FORMAT):
        raise ProtocolError("connection closed before length prefix was received")
    (body_length,) = struct.unpack(_LENGTH_PREFIX_FORMAT, length_prefix)
    if body_length == 0:
        raise ProtocolError("zero-length frames are invalid")

    body = _read_exact(sock, body_length)
    if len(body) != body_length:
        raise ProtocolError("connection closed before frame was fully received")

    try:
        return json.loads(body.decode("utf-8"))
    except json.JSONDecodeError as exc:
        raise ProtocolError(f"invalid JSON payload: {exc}") from exc


def _read_exact(sock: socket.socket, length: int) -> bytes:
    chunks: list[bytes] = []
    bytes_remaining = length
    while bytes_remaining > 0:
        chunk = sock.recv(bytes_remaining)
        if chunk == b"":
            break
        chunks.append(chunk)
        bytes_remaining -= len(chunk)
    return b"".join(chunks)
