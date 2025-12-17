"""Helpers to exercise the length-prefixed JSON framing against the echo stub."""

from __future__ import annotations

import argparse
import socket
from typing import Any, Dict, Iterable, List

from tools.clients.python import echo_stub


def _probe_frames(host: str, port: int, messages: Iterable[Dict[str, Any]]) -> List[Dict[str, Any]]:
    responses: List[Dict[str, Any]] = []
    with socket.create_connection((host, port)) as sock:
        for message in messages:
            echo_stub.write_frame(sock, echo_stub.compact_json(message))
            payload = echo_stub.read_frame(sock)
            if payload is None:
                raise RuntimeError("Server closed connection while probing")
            responses.append(echo_stub.decode_json(payload))
    return responses


def _round_trip_examples(host: str, port: int) -> List[Dict[str, Any]]:
    messages = [
        {"txn_id": 1, "operations": [{"type": "set", "key": "alpha", "value": {"kind": "string", "data": "bravo"}}]},
        {"txn_id": 2, "operations": [{"type": "get", "key": "alpha"}]},
        {"txn_id": 3, "operations": [{"type": "del", "key": "alpha"}]},
    ]
    return _probe_frames(host, port, messages)


def run_self_test() -> None:
    server, thread = echo_stub.start_echo_server()
    host, port = server.server_address
    try:
        responses = _round_trip_examples(host, port)
        expected = [
            {"txn_id": 1, "state": "committed", "operations": [{"type": "set", "key": "alpha", "success": True}]},
            {
                "txn_id": 2,
                "state": "committed",
                "operations": [{"type": "get", "key": "alpha", "success": True, "value": {"kind": "string", "data": "bravo"}}],
            },
            {"txn_id": 3, "state": "committed", "operations": [{"type": "del", "key": "alpha", "success": True}]},
        ]
        if responses != expected:
            raise AssertionError(f"Unexpected responses: {responses!r}")
    finally:
        echo_stub.stop_echo_server(server)
        thread.join(timeout=5)


def main() -> None:
    parser = argparse.ArgumentParser(description="Probe framing against the echo stub")
    parser.add_argument("--host", default="127.0.0.1", help="Echo server host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=0, help="Echo server port (0 = start a local stub)")
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="Start a local stub and exercise set/get/del examples",
    )
    args = parser.parse_args()

    if args.self_test:
        run_self_test()
        print("Self-test passed: framing and echo responses match expectations.")
        return

    if args.port == 0:
        raise SystemExit("When not running --self-test, provide --port to probe an existing stub")

    responses = _round_trip_examples(args.host, args.port)
    for index, response in enumerate(responses, start=1):
        print(f"Response {index}: {response}")


if __name__ == "__main__":
    main()
