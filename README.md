# Jubilant DB

[![CI](https://github.com/maximxlss/jubilant-db/actions/workflows/ci.yml/badge.svg)](https://github.com/maximxlss/jubilant-db/actions/workflows/ci.yml)
[![CMake Presets](https://img.shields.io/badge/build-CMake%20presets-blue)](CMakePresets.json)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Status: Active Scaffolding](https://img.shields.io/badge/status-active%20scaffolding-orange)](MAIN_SPECIFICATION.md)

Jubilant DB is a single-node, hybrid memory+disk key–value store for teams that want a predictable, CLI-friendly database that can grow into server-backed deployments. It pairs a **B+Tree + WAL + value log** storage engine with **strict two-phase locking** and a **length-prefixed network envelope** so local testing and remote clients share the same semantics.

## Release status

- **Current version: v0.0.2 (networked transaction preview).** The JSON wire framing, Python client bundle, server bootstrap, and `jubectl --remote` flow are in place.
- **Outstanding for true v0.0.2 sign-off:** end-to-end integration tests that drive `set/get/del` through the network stack and verify durable replay on restart.
- Historical acceptance notes for the initial CLI-only milestone remain in [`FIRST_STEPS.md`](FIRST_STEPS.md).

## What you get today

- **Local operations:** `jubectl init/set/get/del` exercise the storage engine end to end with UTF-8 key validation and overwrite semantics.
- **Networked preview:** `jubectl --remote`, the Python client bundle, and the server bootstrap share the same JSON envelope so client teams can test without inventing their own framing.
- **Durability guardrails:** Monotonic MANIFEST generations, dual superblocks with CRC selection, and WAL replay on startup to keep crash recovery predictable.
- **Early observability:** `jubectl stats` and `jubectl validate` surface manifest/superblock metadata, checkpoint progress, and corruption checks.

## Quickstart

1. Configure a Debug build tree:

   ```sh
   cmake --preset dev-debug
   ```

2. Build the CLI, libraries, and tests:

   ```sh
   cmake --build --preset dev-debug
   ```

3. Run the test suite:

   ```sh
   ctest --preset dev-debug
   ```

### Run the CLI

Use the shipped `jubectl` to create and interact with a local database directory:

```sh
jubectl init <db_dir>
jubectl set <db_dir> <key> <bytes|string|int> <value>
jubectl get <db_dir> <key>
jubectl del <db_dir> <key>
jubectl stats <db_dir>
jubectl validate <db_dir>
```

Keys must be non-empty UTF-8 strings. Values may be raw bytes (hex), UTF-8 strings, or signed 64-bit integers. `stats` prints manifest/superblock metadata and checkpoint state; `validate` replays manifest and superblock validation to flag corruption.

Remote envelope mode (v0.0.2) reuses the same JSON framing described in
[`docs/txn-wire-v0.0.2.md`](docs/txn-wire-v0.0.2.md):

```sh
jubectl --remote 127.0.0.1:6767 set <key> <bytes|string|int> <value>
jubectl --remote 127.0.0.1:6767 get <key>
jubectl --remote 127.0.0.1:6767 del <key>
jubectl --remote 127.0.0.1:6767 --txn-id 42 txn txn.json
```

Transaction files may include a full request object or just an `operations` array; the CLI injects a
transaction id when one is not present. Byte values are passed as hex on the CLI and base64-encoded on
the wire.

### Prototype Python client (v0.0.2 envelope)

The `tools/clients/python/jubilant_client.py` module speaks the length-prefixed JSON envelope defined in [`docs/txn-wire-v0.0.2.md`](docs/txn-wire-v0.0.2.md). A thin CLI wrapper is available:

```sh
python tools/clients/python/jubectl_client.py --host 127.0.0.1 --port 6767 set alpha string bravo
python tools/clients/python/jubectl_client.py --host 127.0.0.1 --port 6767 get alpha
python tools/clients/python/jubectl_client.py --host 127.0.0.1 --port 6767 del alpha
```

Byte values are passed as hex on the CLI and base64-encoded on the wire. An optional `--txn-id` supports deterministic testing.
All Python client scripts are staged into `build/<preset>/python_clients/` by the `python_clients` target, which is included in the default presets. Use `cmake --build --preset dev-debug-server` to build just the server binary and Python bundle after configuring.

## Server bootstrap (v0.0.2)

The `jubildb_server` binary loads a TOML configuration, initializes storage, starts the worker pool,
and binds the network adapter described in [`docs/txn-wire-v0.0.2.md`](docs/txn-wire-v0.0.2.md).

```sh
cmake --build --preset dev-debug-server
./build/dev-debug/jubildb_server --config ./server.toml --workers 4
```

`--config` points to the TOML file consumed by `ConfigLoader`; `--workers` defaults to the number
of hardware threads when omitted. The listener binds to `listen_address`/`listen_port` from the
config and advertises the bound port after any OS reassignment.

## Configuration

`jubildb` processes read TOML configuration files through `ConfigLoader` with defaults for every field except the database path. A minimal config is only a path:

```toml
db_path = "/var/lib/jubildb"
```

Optional settings are validated on load (non-zero page size, inline threshold within the page, positive cache size, listen port within `0-65535`, with `0` requesting an ephemeral binding):

```toml
db_path = "./data"
page_size = 8192
inline_threshold = 2048
group_commit_max_latency_ms = 12
cache_bytes = 134217728
listen_address = "0.0.0.0"
listen_port = 7777
```

Defaults mirror the current implementation: 4 KiB pages, 1 KiB inline threshold, a 64 MiB cache, 5 ms max group-commit latency,
and `127.0.0.1:6767` for the listening socket. The `jubildb_server` bootstrap uses the same file to seed on-disk metadata (page
size and inline threshold) on first run and to determine the binding address for the network adapter.

Set `listen_port = 0` in the configuration to request an ephemeral port during tests; the bound port is emitted in the startup
banner so clients can discover the dynamic endpoint.

## Documentation map

- **Developer docs index:** [`docs/README.md`](docs/README.md)
- **Product/storage spec:** [`MAIN_SPECIFICATION.md`](MAIN_SPECIFICATION.md)
- **Technical stack:** [`TECH_SPECIFICATION.md`](TECH_SPECIFICATION.md)
- **Server runtime scaffolding:** [`docs/server-runtime.md`](docs/server-runtime.md)
- **Unified roadmap:** [`docs/roadmap.md`](docs/roadmap.md) with milestone detail in [`docs/v0.0.2-plan.md`](docs/v0.0.2-plan.md)

## Build and contribution workflow

- Use the CMake presets in `CMakePresets.json` (`dev-debug` for daily work, `dev-debug-tidy` for linting).
- Format before committing: `cmake --build --preset dev-debug --target clang-format`.
- `clang-tidy` diagnostics must be addressed; presets include tidy-friendly options.
- Conventional Commits are required for commit messages.
- See [`CONTRIBUTING.md`](CONTRIBUTING.md) for contributor expectations and developer setup tips.

## Repository layout

- `schemas/` — FlatBuffers definitions for wire, WAL, and disk formats.
- `src/` — Engine sources (pager, B+Tree, WAL, TTL, cache, server runtime).
- `tools/` — CLI (`tools/jubectl/`) and client prototypes (`tools/clients/python/`).
- `tests/` — Unit and integration coverage.
- `docs/` — Documentation index and topic guides.
- `cmake/` — Shared CMake helpers and toolchain settings.

## Roadmap snapshot

- **v0.0.2:** Networked transactions are available for experimentation. The remaining release blocker is integration coverage that drives the JSON envelope through the C++ server to prove durability and restart safety.
- **Next milestones:** Continued server/runtime hardening plus observability, validation, and long-range HA work are tracked in [`docs/roadmap.md`](docs/roadmap.md).

## License

Jubilant DB is available under the [MIT License](LICENSE), encouraging reuse and contribution without restrictive terms.
