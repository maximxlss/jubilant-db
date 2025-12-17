# Transaction wire contract (v0.0.2)

This document freezes the minimal request/response contract for v0.0.2 so the
server network adapter, Python client, and `jubectl` can interoperate while the
rest of the network stack is still under development.

## Transport and framing

* **Connection:** TCP stream. Clients may reuse a connection for multiple
  transactions; each frame represents one request or one response.
* **Encoding:** UTF-8 JSON payloads.
* **Framing:** A 4-byte unsigned length prefix in **network byte order**
  (`uint32_t`, big-endian) precedes every JSON payload. The prefix covers only
  the JSON byte length (no sentinel or checksum). Servers must treat incomplete
  frames as malformed and close the connection. Individual frames are capped at
  **1 MiB** for v0.0.2 to prevent unbounded allocations; larger prefixes are
  rejected and the connection is closed.
* **Pairing:** Each request yields exactly one response with the same
  `txn_id`. Responses are emitted as soon as the transaction finishes; no
  batching or multiplexing is defined for v0.0.2.

## Envelope fields

### Request

| Field | Type | Notes |
| --- | --- | --- |
| `txn_id` | unsigned integer | 64-bit transaction id. Keep values within `0 .. 2^63-1` to avoid JSON precision loss. Retries should reuse the same id so duplicate submissions can be detected once server-side replay protection lands. |
| `operations` | array | Ordered list of operations executed sequentially inside the transaction. At least one entry is required. |
| `operations[].type` | string | One of `"get"`, `"set"`, `"del"`. |
| `operations[].key` | string | UTF-8 key. Empty strings are invalid. |
| `operations[].value` | object | Required for `set`, forbidden for `del`, optional for `get` (ignored if present). Encodes the target `storage::btree::Record`. |

`value` is a discriminated union:

| Field | Type | Notes |
| --- | --- | --- |
| `kind` | string | One of `"bytes"`, `"string"`, `"int"`. Maps to `ValueType::kBytes`, `ValueType::kString`, or `ValueType::kInt64`. |
| `data` | string or integer | For `bytes`, base64-encoded raw bytes. For `string`, a UTF-8 string. For `int`, a signed 64-bit integer (`-2^63 .. 2^63-1`). |
| `metadata` | object (optional) | Reserved for TTL and similar extensions. When omitted, defaults apply (e.g., TTL of `0`). |

### Response

| Field | Type | Notes |
| --- | --- | --- |
| `txn_id` | unsigned integer | Mirrors the request id. |
| `state` | string | `"committed"` or `"aborted"` (maps to `TransactionState`). |
| `operations` | array | Same length and order as the request. |
| `operations[].type` | string | Echoes the request `type`. |
| `operations[].key` | string | Echoes the request `key`. |
| `operations[].success` | boolean | Indicates whether the operation succeeded. For aborted transactions this may be `false` for all entries. |
| `operations[].value` | object (optional) | Present when a `get` returns a value or when the server chooses to echo stored data after a `set`. Shares the request `value` shape. |

## JSON schema (draft)

Request schema:

```json
{
  "type": "object",
  "required": ["txn_id", "operations"],
  "properties": {
    "txn_id": {"type": "integer", "minimum": 0, "maximum": 9223372036854775807},
    "operations": {
      "type": "array",
      "minItems": 1,
      "items": {
        "type": "object",
        "required": ["type", "key"],
        "properties": {
          "type": {"enum": ["get", "set", "del"]},
          "key": {"type": "string", "minLength": 1},
          "value": {
            "type": "object",
            "required": ["kind", "data"],
            "properties": {
              "kind": {"enum": ["bytes", "string", "int"]},
              "data": {
                "oneOf": [
                  {"type": "string"},
                  {"type": "integer", "minimum": -9223372036854775808, "maximum": 9223372036854775807}
                ]
              },
              "metadata": {"type": "object"}
            },
            "additionalProperties": true
          }
        },
        "additionalProperties": true
      }
    }
  },
  "additionalProperties": true
}
```

Response schema:

```json
{
  "type": "object",
  "required": ["txn_id", "state", "operations"],
  "properties": {
    "txn_id": {"type": "integer", "minimum": 0, "maximum": 9223372036854775807},
    "state": {"enum": ["committed", "aborted"]},
    "operations": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["type", "key", "success"],
        "properties": {
          "type": {"enum": ["get", "set", "del"]},
          "key": {"type": "string", "minLength": 1},
          "success": {"type": "boolean"},
          "value": {
            "type": "object",
            "required": ["kind", "data"],
            "properties": {
              "kind": {"enum": ["bytes", "string", "int"]},
              "data": {
                "oneOf": [
                  {"type": "string"},
                  {"type": "integer", "minimum": -9223372036854775808, "maximum": 9223372036854775807}
                ]
              },
              "metadata": {"type": "object"}
            },
            "additionalProperties": true
          }
        },
        "additionalProperties": true
      }
    }
  },
  "additionalProperties": true
}
```

Servers and clients **must** reject frames that violate these shapes. Unknown
fields are allowed for forward compatibility and should be ignored.

## Execution and compatibility rules

* Operations execute in request order under a single transaction id.
* `get` on a missing key should set `success=false` and omit `value`.
* `set` overwrites existing keys; the server may return the stored value in the
  response for observability but is not required to do so in v0.0.2.
* `del` reports `success=false` when no key was removed.
* Malformed frames (bad length prefix, invalid JSON, or schema violations) must
  be rejected by closing the connection. Duplicate in-flight `txn_id` values are
  aborted with a response that mirrors the requested operations and
  `state="aborted"`.
* Connections may carry multiple back-to-back frames. The receiver must not
  treat framing boundaries as message boundaries inside the JSON payload (the
  entire payload belongs to a single frame).
* Backward/forward compatibility: new optional fields may be added in future
  versions; numeric ranges should remain within the documented bounds to avoid
  cross-language precision loss.

## Worked examples

Length prefixes below are shown in hexadecimal (`!I` network order) followed by
the compact JSON payload.

### `set`

Prefix `0x00000061` (97 bytes), payload:

```json
{"txn_id":1,"operations":[{"type":"set","key":"alpha","value":{"kind":"string","data":"bravo"}}]}
```

### `get`

Prefix `0x00000038` (56 bytes), payload:

```json
{"txn_id":2,"operations":[{"type":"get","key":"alpha"}]}
```

Possible response for a successful read:

Prefix `0x00000084` (132 bytes), payload:

```json
{"txn_id":2,"state":"committed","operations":[{"type":"get","key":"alpha","success":true,"value":{"kind":"string","data":"bravo"}}]}
```

### `del`

Prefix `0x00000038` (56 bytes), payload:

```json
{"txn_id":3,"operations":[{"type":"del","key":"alpha"}]}
```

Successful delete response:

Prefix `0x0000005b` (91 bytes), payload:

```json
{"txn_id":3,"state":"committed","operations":[{"type":"del","key":"alpha","success":true}]}
```

These examples use compact JSON (`","` separators) to make length prefixes
deterministic; whitespace may be added if both sides agree to recompute lengths
accordingly.

## Prototyping harness

Run the Python echo stub to validate framing before the C++ network adapter is
ready:

```
python -m tools.clients.python.echo_stub --host 127.0.0.1 --port 9000
```

To exercise the worked examples against the stub without keeping the server
running, use the probe’s self-test (it starts the stub on an ephemeral port,
issues set/get/del, and asserts the echoed responses):

```
python -m tools.clients.python.framing_probe --self-test
```
