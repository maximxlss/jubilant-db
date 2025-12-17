from __future__ import annotations

import argparse
import json
import sys
from typing import Any, Optional

from jubilant_client import (
    DEFAULT_HOST,
    DEFAULT_PORT,
    ProtocolError,
    connect,
    delete_value,
    get_value,
    set_value,
)


def _parse_args(argv: Optional[list[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Minimal Python client for Jubilant DB's v0.0.2 wire contract. "
            "Requires a server that speaks docs/txn-wire-v0.0.2.md."
        )
    )
    parser.add_argument("--host", default=DEFAULT_HOST, help="Server hostname (default: %(default)s)")
    parser.add_argument(
        "--port", default=DEFAULT_PORT, type=int, help="Server TCP port (default: %(default)s)"
    )
    parser.add_argument(
        "--timeout", default=5.0, type=float, help="Socket timeout in seconds (default: %(default)s)"
    )
    parser.add_argument("--txn-id", type=int, help="Optional transaction id (otherwise auto-generated)")

    subparsers = parser.add_subparsers(dest="command", required=True)

    set_parser = subparsers.add_parser("set", help="Set a key to the given value")
    set_parser.add_argument("key", help="UTF-8 key to write")
    set_parser.add_argument("value_type", choices=["bytes", "string", "int"], help="Stored value type")
    set_parser.add_argument("value", help="Value to store (hex for bytes)")

    get_parser = subparsers.add_parser("get", help="Fetch a key")
    get_parser.add_argument("key", help="UTF-8 key to read")

    del_parser = subparsers.add_parser("del", help="Delete a key")
    del_parser.add_argument("key", help="UTF-8 key to remove")

    return parser.parse_args(argv)


def _coerce_value(value_type: str, value: str) -> Any:
    if value_type == "bytes":
        try:
            return bytes.fromhex(value)
        except ValueError as exc:
            raise ValueError("byte values must be provided as hex-encoded strings") from exc
    if value_type == "string":
        return value
    if value_type == "int":
        try:
            return int(value)
        except ValueError as exc:
            raise ValueError("int values must be parseable as signed 64-bit integers") from exc
    raise ValueError("unsupported value type")


def main(argv: Optional[list[str]] = None) -> int:
    args = _parse_args(argv)

    try:
        with connect(args.host, args.port, args.timeout) as sock:
            txn_id = args.txn_id
            if args.command == "set":
                payload = _coerce_value(args.value_type, args.value)
                response = set_value(sock, args.key, payload, kind=args.value_type, txn_id=txn_id)
            elif args.command == "get":
                response = get_value(sock, args.key, txn_id=txn_id)
            elif args.command == "del":
                response = delete_value(sock, args.key, txn_id=txn_id)
            else:
                raise ValueError(f"unknown command {args.command}")

        print(json.dumps(response, indent=2, sort_keys=True))
        return 0
    except (OSError, ValueError, ProtocolError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
