# Jubilant DB

[![CI](https://github.com/maximxlss/jubilant-db/actions/workflows/ci.yml/badge.svg)](https://github.com/maximxlss/jubilant-db/actions/workflows/ci.yml)
[![CMake Presets](https://img.shields.io/badge/build-CMake%20presets-blue)](CMakePresets.json)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Status: Active Scaffolding](https://img.shields.io/badge/status-active%20scaffolding-orange)](MAIN_SPECIFICATION.md)

Jubilant DB is a single-node, hybrid memory+disk key–value store. It targets a B+Tree plus WAL/value-log storage engine, strict two-phase locking, and a `jubectl` CLI that exercises initialization and CRUD paths end to end.

## At a glance

* **Docs:** A curated index lives in [`docs/README.md`](docs/README.md); product and technical specs remain in `MAIN_SPECIFICATION.md` and `TECH_SPECIFICATION.md`.
* **Current milestone (v0.0.1):** `jubectl init/set/get/del` flows are implemented through `SimpleStore` with UTF-8 key validation, overwrite semantics, and persistence across clean restarts. Coverage comes from unit tests for the B+Tree, pager, manifest/superblock skeleton, and the CLI-facing store wrapper. See [`FIRST_STEPS.md`](FIRST_STEPS.md) for acceptance criteria and the tests under `tests/` for evidence.
* **Build + test quickly:** Configure with `cmake --preset dev-debug`, build via `cmake --build --preset dev-debug`, and run `ctest --preset dev-debug`.

## CLI quickstart

The `jubectl` tool exercises the v0.0.1 surface area:

```sh
jubectl init <db_dir>
jubectl set <db_dir> <key> <bytes|string|int> <value>
jubectl get <db_dir> <key>
jubectl del <db_dir> <key>
```

Keys must be valid UTF-8 and non-empty; values may be raw bytes (hex), UTF-8 strings, or signed 64-bit integers.

## Build + contribute

The repository standardizes on CMake presets (3.25+ with Ninja recommended):

1. Configure a Debug tree with tests enabled: `cmake --preset dev-debug`
2. Build artifacts and utilities: `cmake --build --preset dev-debug`
3. Run tests: `ctest --preset dev-debug`

Linting-friendly presets exist (`dev-debug-tidy`), and `clang-format`/`clang-tidy` targets are available from any configured build tree.

## Roadmap

Short-term steps beyond v0.0.1:

1. **Storage durability sweep:** Persist manifest generations, dual superblocks with CRC selection, and a write-ahead log replayed at startup.
2. **B+Tree + pager completeness:** Finish page allocation and on-disk B+Tree layout, enforce page-level checksums, and plumb inline/value-log routing.
3. **CLI and validation growth:** Extend `jubectl` with stats and validation commands, wired into the manifest/superblock metadata and storage checkpoints.

For a longer-horizon view, see [`FUTURE_UPDATES.md`](FUTURE_UPDATES.md).

## Repository layout

* `schemas/` — FlatBuffers definitions for wire, WAL, and disk formats.
* `src/` — core engine sources (pager, B+Tree, WAL, TTL, cache, server runtime).
* `tools/jubectl/` — CLI sources for `jubectl`.
* `tests/` — unit and integration tests.
* `cmake/` — shared CMake helpers/toolchain settings.

## License

Jubilant DB is available under the [MIT License](LICENSE), encouraging reuse and contribution without restrictive terms.
