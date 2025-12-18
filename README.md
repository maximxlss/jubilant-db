# Jubilant DB

[![CI](https://github.com/maximxlss/jubilant-db/actions/workflows/ci.yml/badge.svg)](https://github.com/maximxlss/jubilant-db/actions/workflows/ci.yml)
[![CMake Presets](https://img.shields.io/badge/build-CMake%20presets-blue)](CMakePresets.json)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Status: Active Scaffolding](https://img.shields.io/badge/status-active%20scaffolding-orange)](MAIN_SPECIFICATION.md)

Jubilant DB is a single-node key–value database that pairs **Redis-like speed** with **explicit serializable transactions**, a **parallel request pipeline**, and **zero-ops packaging**. The north star is a polished v1 that stays fast on one box while keeping teams confident in concurrency, durability, and an embeddable protocol.

## Who it’s for (at v1)

- Product teams shipping feature flags, personalization, or job metadata without standing up a fleet.
- API builders who want a compact, C-compatible binary protocol that works the same on embedded devices and cloud VMs.
- Operators who prefer predictable single-node resilience with transparent repair flows and observability.

## Release status

- **Current version: v0.0.2 (networked transaction preview).** Remote JSON framing, the Python client bundle, the C++ server bootstrap, and `jubectl --remote` exist so you can trial remote calls.
- **Not yet production-ready:** we still owe end-to-end integration coverage that drives `set/get/del` through the network stack and proves durable replay on restart.

## What ships today

- **Local CLI:** `jubectl init/set/get/del/stats/validate` let you spin up a DB directory, mutate keys, and check metadata without extra services.
- **Remote preview:** `jubectl --remote` and the Python client share the JSON envelope from [`docs/txn-wire-v0.0.2.md`](docs/txn-wire-v0.0.2.md).
- **Durability guardrails:** manifest tracking, mirrored superblocks, and WAL replay on startup keep the database recoverable after crashes.
- **Early observability:** `jubectl stats` and `jubectl validate` surface what’s being written and whether on-disk structures pass integrity checks.

## Parallel-first design pillars

- **Worker pool + deterministic locking:** strict 2PL with canonical key ordering lets independent transactions execute in parallel while avoiding deadlocks.
- **Group-commit durability:** WAL + value-log writes batch safely without starving latency-sensitive requests.
- **Overlay planner:** transactions stage edits before mutation so high-concurrency paths stay predictable.

## Where we’re headed

- **v0.0.2 acceptance:** land the integration suite that exercises the network path and restart replay.
- **Broader data model (post-v1):** add typed collections and richer data shapes once the core KV is hardened.
- **Authorization and governance (post-v1):** role-based access, auditing, and policy hooks for multi-team deployments.
- **Operational polish:** config reloads, parallel background maintenance, and connectors/backup tooling to make a single binary production-friendly.

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

### Local CLI

```sh
jubectl init <db_dir>
jubectl set <db_dir> <key> <bytes|string|int> <value>
jubectl get <db_dir> <key>
jubectl del <db_dir> <key>
jubectl stats <db_dir>
jubectl validate <db_dir>
```

Values may be raw bytes (hex), UTF-8 strings, or signed 64-bit integers. Keys must be non-empty UTF-8 strings.

### Remote preview (v0.0.2)

```sh
jubectl --remote 127.0.0.1:6767 set <key> <bytes|string|int> <value>
jubectl --remote 127.0.0.1:6767 get <key>
jubectl --remote 127.0.0.1:6767 del <key>
jubectl --remote 127.0.0.1:6767 --txn-id 42 txn txn.json
```

Transaction files may include a full request object or just an `operations` array; the CLI injects a transaction id when one is not present.

## More detail

- **Docs index:** [`docs/README.md`](docs/README.md)
- **Product/storage spec:** [`MAIN_SPECIFICATION.md`](MAIN_SPECIFICATION.md)
- **Technical stack:** [`TECH_SPECIFICATION.md`](TECH_SPECIFICATION.md)
- **Unified roadmap:** [`docs/roadmap.md`](docs/roadmap.md)

## License

Jubilant DB is available under the [MIT License](LICENSE), encouraging reuse and contribution without restrictive terms.
