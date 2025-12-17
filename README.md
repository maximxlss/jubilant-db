# Jubilant DB

[![CI](https://github.com/maximxlss/jubilant-db/actions/workflows/ci.yml/badge.svg)](https://github.com/maximxlss/jubilant-db/actions/workflows/ci.yml)
[![CMake Presets](https://img.shields.io/badge/build-CMake%20presets-blue)](CMakePresets.json)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Status: Active Scaffolding](https://img.shields.io/badge/status-active%20scaffolding-orange)](MAIN_SPECIFICATION.md)

Jubilant DB is a single-node, hybrid memory+disk key–value store built around a **B+Tree + WAL + value log** storage engine, **strict two-phase locking**, and a **C-compatible protocol**. The project aims to feel like a production system even while it grows: clear specs, predictable builds, and test coverage that tracks every milestone.

## What you get today

- **CLI-first store:** `jubectl init/set/get/del` exercise the storage engine end to end with UTF-8 key validation and overwrite semantics.
- **Durability guardrails:** Monotonic MANIFEST generations, dual superblocks with CRC selection, and a WAL replayed on startup.
- **Early observability:** `jubectl stats` and `jubectl validate` surface manifest/superblock metadata, checkpoint progress, and corruption checks.
- **Server scaffolding:** The transaction receiver, worker pool, and completion queue under `src/server/` let wire-protocol work focus on dispatch and correctness.

Current coverage for v0.0.1 acceptance criteria lives in [`FIRST_STEPS.md`](FIRST_STEPS.md), with tests under `tests/` exercising CRUD, persistence, pager IO, and WAL replay.

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

Remote envelope mode (v0.0.2 draft) reuses the same JSON framing described in
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

## Configuration

`jubildb` processes read TOML configuration files through `ConfigLoader` with defaults for every field except the database path. A minimal config is only a path:

```toml
db_path = "/var/lib/jubildb"
```

Optional settings are validated on load (non-zero page size, inline threshold within the page, positive cache size, listen port within `1-65535`):

```toml
db_path = "./data"
page_size = 8192
inline_threshold = 2048
group_commit_max_latency_ms = 12
cache_bytes = 134217728
listen_address = "0.0.0.0"
listen_port = 7777
```

Defaults today: 4 KiB pages, 1 KiB inline threshold, 64 MiB cache, 5 ms max group-commit latency, and a listener on `127.0.0.1:6767`.

## Documentation map

- **Developer docs index:** [`docs/README.md`](docs/README.md)
- **Product/storage spec:** [`MAIN_SPECIFICATION.md`](MAIN_SPECIFICATION.md)
- **Technical stack:** [`TECH_SPECIFICATION.md`](TECH_SPECIFICATION.md)
- **Server runtime scaffolding:** [`docs/server-runtime.md`](docs/server-runtime.md)
- **Server roadmap + milestones:** [`docs/server-roadmap.md`](docs/server-roadmap.md) and [`FUTURE_UPDATES.md`](FUTURE_UPDATES.md)

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

- **v0.0.1 (in progress):** CLI-driven storage with WAL-backed durability and validation utilities. See [`FIRST_STEPS.md`](FIRST_STEPS.md) for the acceptance checklist.
- **Server buildout:** The runtime scaffolding is staged; wire protocol, TTL enforcement, and transactional flows are the next focus. Longer-horizon work lives in [`FUTURE_UPDATES.md`](FUTURE_UPDATES.md).

## License

Jubilant DB is available under the [MIT License](LICENSE), encouraging reuse and contribution without restrictive terms.
