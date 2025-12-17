# Jubilant DB

[![CI](https://github.com/maximxlss/jubilant-db/actions/workflows/ci.yml/badge.svg)](https://github.com/maximxlss/jubilant-db/actions/workflows/ci.yml)
[![CMake Presets](https://img.shields.io/badge/build-CMake%20presets-blue)](CMakePresets.json)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Status: Active Scaffolding](https://img.shields.io/badge/status-active%20scaffolding-orange)](MAIN_SPECIFICATION.md)

Jubilant DB is a single-node, hybrid memory+disk key–value store. It targets a B+Tree plus WAL/value-log storage engine, strict two-phase locking, and a `jubectl` CLI that exercises initialization and CRUD paths end to end.

## At a glance

* **Docs:** A curated index lives in [`docs/README.md`](docs/README.md); product and technical specs remain in `MAIN_SPECIFICATION.md` and `TECH_SPECIFICATION.md`.
* **Current milestone (v0.0.1):** `jubectl init/set/get/del` flows are implemented through `SimpleStore` with UTF-8 key validation, overwrite semantics, and persistence across clean restarts. Durability now includes monotonic manifest generations, CRC-protected dual superblocks, and a write-ahead log that replays on startup. CLI observability is expanding with `stats` and `validate` commands that surface manifest/superblock metadata and checkpoint progress. Coverage comes from unit tests for the B+Tree, pager, manifest/superblock rotation, WAL replay, and the CLI-facing store wrapper. See [`FIRST_STEPS.md`](FIRST_STEPS.md) for acceptance criteria and the tests under `tests/` for evidence.
* **Build + test quickly:** Configure with `cmake --preset dev-debug`, build via `cmake --build --preset dev-debug`, and run `ctest --preset dev-debug`.
* **Server runtime scaffolding:** A bounded transaction receiver, worker pool, and completion queue live under `src/server/` so wire-protocol work can focus on dispatch without reworking lifecycle management. See [`docs/server-runtime.md`](docs/server-runtime.md).

## CLI quickstart

The `jubectl` tool exercises the v0.0.1 surface area:

```sh
jubectl init <db_dir>
jubectl set <db_dir> <key> <bytes|string|int> <value>
jubectl get <db_dir> <key>
jubectl del <db_dir> <key>
jubectl stats <db_dir>
jubectl validate <db_dir>
```

Keys must be valid UTF-8 and non-empty; values may be raw bytes (hex), UTF-8 strings, or signed 64-bit integers.

* `stats` prints manifest generation/version, page sizing, the active superblock’s root page and checkpoint LSN, and current page/key counts.
* `validate` replays manifest validation rules and superblock CRC selection to report corruption or missing metadata.

## Python client (v0.0.2 prototype)

The `tools/clients/python/jubilant_client.py` module speaks the v0.0.2 JSON envelope with a length prefix as defined in [`docs/txn-wire-v0.0.2.md`](docs/txn-wire-v0.0.2.md). A thin CLI wrapper exercises the helpers:

```sh
python tools/clients/python/jubectl_client.py --host 127.0.0.1 --port 6767 set alpha string bravo
python tools/clients/python/jubectl_client.py --host 127.0.0.1 --port 6767 get alpha
python tools/clients/python/jubectl_client.py --host 127.0.0.1 --port 6767 del alpha
```

Byte values are supplied as hex strings and base64-encoded on the wire; the CLI accepts an optional `--txn-id` for deterministic testing.

## Configuration

`jubildb` instances read TOML configuration files through `ConfigLoader` with sensible defaults for every field except the
database path. A minimal configuration contains only the path:

```toml
db_path = "/var/lib/jubildb"
```

Additional settings are optional and validated on load (non-zero page size, inline threshold within the page, positive cache
size, and a listen port within `1-65535`):

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
and `127.0.0.1:6767` for the listening socket.

## Build + contribute

The repository standardizes on CMake presets (3.25+ with Ninja recommended):

1. Configure a Debug tree with tests enabled: `cmake --preset dev-debug`
2. Build artifacts and utilities: `cmake --build --preset dev-debug`
3. Run tests: `ctest --preset dev-debug`

Before opening a pull request, run `cmake --build --preset dev-debug --target clang-format` to keep diffs clean. CI enforces formatting but will fail instead of auto-fixing.

Linting-friendly presets exist (`dev-debug-tidy`), and `clang-format`/`clang-tidy` targets are available from any configured build tree.

## Roadmap

Short-term steps beyond v0.0.1:

1. **(Done) Storage durability sweep:** Persist manifest generations, dual superblocks with CRC selection, and a write-ahead log replayed at startup.
2. **(Done) B+Tree + pager completeness:** Page allocation now writes chained leaf pages with CRC-guarded headers, and large values flow through the value log while small values stay inline.
3. **(Done) CLI and validation growth:** `jubectl` now ships `stats` and `validate` commands wired into manifest/superblock metadata and storage checkpoints.

For a longer-horizon view, see [`FUTURE_UPDATES.md`](FUTURE_UPDATES.md).

## Repository layout

* `schemas/` — FlatBuffers definitions for wire, WAL, and disk formats.
* `src/` — core engine sources (pager, B+Tree, WAL, TTL, cache, server runtime).
* `tools/jubectl/` — CLI sources for `jubectl`.
* `tests/` — unit and integration tests.
* `cmake/` — shared CMake helpers/toolchain settings.

## License

Jubilant DB is available under the [MIT License](LICENSE), encouraging reuse and contribution without restrictive terms.
