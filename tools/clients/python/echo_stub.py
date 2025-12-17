"""Length-prefixed echo server for the v0.0.2 wire protocol.

This stub receives JSON requests framed with a 4-byte network-order length
prefix and responds with a minimal transaction-shaped echo. It keeps a
process-local dictionary to surface basic `get/set/del` behavior so framing
and payload structure can be exercised before the C++ network adapter lands.

Run as a module to start a server:

    python -m tools.clients.python.echo_stub --host 127.0.0.1 --port 9000

The server binds to an ephemeral port by default and logs the chosen endpoint
to stdout. Responses mirror the request `txn_id`, mark the transaction as
`committed`, and include per-operation success flags with optional values.
"""

from __future__ import annotations

import argparse
import json
import socket
import socketserver
import struct
import threading
from typing import Any, Dict, Iterable, List, Optional, Tuple


class FrameProtocolError(Exception):
    """Raised when a frame cannot be parsed according to the contract."""


LENGTH_PREFIX_FORMAT = "!I"
MAX_FRAME_SIZE_BYTES = 8 * 1024 * 1024  # 8 MiB safety guard.


def _read_exact(sock: socket.socket, expected: int) -> Optional[bytes]:
    """Read exactly ``expected`` bytes or return None if the stream closes."""

    chunks: List[bytes] = []
    remaining = expected
    while remaining > 0:
        piece = sock.recv(remaining)
        if piece == b"":
            return None
        chunks.append(piece)
        remaining -= len(piece)
    return b"".join(chunks)


def read_frame(stream: socket.socket) -> Optional[bytes]:
    """Read a single length-prefixed frame from ``stream``.

    Returns None when the peer closes the connection cleanly.
    """

    prefix = _read_exact(stream, 4)
    if prefix is None:
        return None

    (payload_length,) = struct.unpack(LENGTH_PREFIX_FORMAT, prefix)
    if payload_length > MAX_FRAME_SIZE_BYTES:
        raise FrameProtocolError(
            f"Frame length {payload_length} exceeds limit {MAX_FRAME_SIZE_BYTES}"
        )

    payload = _read_exact(stream, payload_length)
    if payload is None:
        raise FrameProtocolError("Connection closed while reading frame payload")
    return payload


def write_frame(stream: socket.socket, payload: bytes) -> None:
    """Write a length-prefixed payload to ``stream``."""

    prefix = struct.pack(LENGTH_PREFIX_FORMAT, len(payload))
    stream.sendall(prefix + payload)


def compact_json(data: Any) -> bytes:
    """Serialize ``data`` to compact JSON bytes (UTF-8)."""

    return json.dumps(data, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def decode_json(payload: bytes) -> Dict[str, Any]:
    """Decode a UTF-8 JSON payload into a dictionary."""

    return json.loads(payload.decode("utf-8"))


class _EchoState:
    """Thread-safe in-memory store for echo responses."""

    def __init__(self) -> None:
        self._store: Dict[str, Any] = {}
        self._lock = threading.Lock()

    def apply(self, operations: Iterable[Dict[str, Any]]) -> List[Dict[str, Any]]:
        """Apply operations sequentially and return response entries."""

        responses: List[Dict[str, Any]] = []
        with self._lock:
            for op in operations:
                op_type = op.get("type")
                key = op.get("key")

                if not isinstance(key, str) or not key:
                    responses.append(
                        {"type": op_type, "key": key, "success": False, "error": "invalid-key"}
                    )
                    continue

                if op_type == "set":
                    value = op.get("value")
                    self._store[key] = value
                    responses.append({"type": "set", "key": key, "success": True})
                elif op_type == "get":
                    if key in self._store:
                        responses.append(
                            {
                                "type": "get",
                                "key": key,
                                "success": True,
                                "value": self._store[key],
                            }
                        )
                    else:
                        responses.append({"type": "get", "key": key, "success": False})
                elif op_type == "del":
                    success = key in self._store
                    if success:
                        self._store.pop(key, None)
                    responses.append({"type": "del", "key": key, "success": success})
                else:
                    responses.append(
                        {"type": op_type, "key": key, "success": False, "error": "unknown-op"}
                    )
        return responses


class _EchoServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, server_address: Tuple[str, int]) -> None:
        super().__init__(server_address, LengthPrefixedEchoHandler)
        self.state = _EchoState()


class LengthPrefixedEchoHandler(socketserver.BaseRequestHandler):
    """Echo handler that validates framing and mirrors transaction envelopes."""

    def handle(self) -> None:  # type: ignore[override]
        while True:
            try:
                payload = read_frame(self.request)
                if payload is None:
                    return

                try:
                    message = decode_json(payload)
                except json.JSONDecodeError as exc:  # pragma: no cover - exercised in manual probes
                    error_body = {"state": "aborted", "error": f"invalid-json: {exc}"}
                    write_frame(self.request, compact_json(error_body))
                    continue

                response = self._build_response(message)
                write_frame(self.request, compact_json(response))
            except FrameProtocolError:
                return

    def _build_response(self, message: Dict[str, Any]) -> Dict[str, Any]:
        txn_id = message.get("txn_id")
        operations = message.get("operations", [])
        if not isinstance(operations, list):
            operations = []

        op_results = self.server.state.apply(operations)  # type: ignore[attr-defined]
        return {
            "txn_id": txn_id,
            "state": "committed",
            "operations": op_results,
        }


def start_echo_server(host: str = "127.0.0.1", port: int = 0) -> Tuple[_EchoServer, threading.Thread]:
    """Start the echo server on ``host:port`` and return the server + thread."""

    server = _EchoServer((host, port))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, thread


def stop_echo_server(server: _EchoServer) -> None:
    """Stop the running echo server."""

    server.shutdown()
    server.server_close()


def main() -> None:
    parser = argparse.ArgumentParser(description="Length-prefixed echo stub for v0.0.2 framing")
    parser.add_argument("--host", default="127.0.0.1", help="Interface to bind (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=0, help="Port to bind (0 = ephemeral)")
    args = parser.parse_args()

    server, thread = start_echo_server(host=args.host, port=args.port)
    bound_host, bound_port = server.server_address
    print(f"Echo stub listening on {bound_host}:{bound_port}. Press Ctrl+C to stop.")

    try:
        thread.join()
    except KeyboardInterrupt:
        print("Shutting down echo stub...")
    finally:
        stop_echo_server(server)


if __name__ == "__main__":
    main()
