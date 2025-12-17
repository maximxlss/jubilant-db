# Documentation index

This index is the entry point for engineers and contributors. It groups the specs, how-to guides, and roadmaps so the project reads like a real product as it grows.

## Start here

- **Project overview:** [`README.md`](../README.md) summarizes goals, quickstart commands, and current status.
- **Contributor expectations:** [`CONTRIBUTING.md`](../CONTRIBUTING.md) covers workflow, commit style, formatting, and presets.
- **Specs:**
  - Product + storage semantics: [`MAIN_SPECIFICATION.md`](../MAIN_SPECIFICATION.md)
  - Tooling and stack recommendations: [`TECH_SPECIFICATION.md`](../TECH_SPECIFICATION.md)

## Architecture and runtime

- **Server runtime scaffolding:** [`server-runtime.md`](server-runtime.md) explains the transaction receiver, worker pool, and completion queue that underpin the future wire protocol.
- **Wire envelope (v0.0.2 prototype):** [`txn-wire-v0.0.2.md`](txn-wire-v0.0.2.md) details the length-prefixed JSON framing and provides sample payloads.
- **CMake/CI setup:** [`ci-setup.md`](ci-setup.md) documents how to reproduce CI locally using presets and helper targets.

## Clients and tooling
* **jubectl remote mode:** `jubectl --remote <host:port>` now speaks the same length-prefixed JSON envelope for `set/get/del` plus a `txn` command that loads a JSON request (or `operations` array) from disk. Provide `--txn-id` to pin the transaction id and `--timeout-ms` to tune socket timeouts.
* **Server bootstrap:** `jubildb_server` reads a TOML config, initializes storage, and starts the network adapter + worker pool. Build via `cmake --build --preset dev-debug --target jubildb_server` and launch with `./build/dev-debug/jubildb_server --config ./server.toml [--workers N]`.
- **Python client prototype:** [`tools/clients/python/`](../tools/clients/python) ships `jubilant_client.py` and the CLI wrapper `jubectl_client.py` for the v0.0.2 envelope. Bytes are provided as hex and base64-encoded on the wire.
- **CLI usage:** The top-level [`README.md`](../README.md) documents `jubectl init/set/get/del/stats/validate` and links to configuration options.

## Milestones and acceptance

- **v0.0.1 acceptance checklist:** [`FIRST_STEPS.md`](../FIRST_STEPS.md) tracks the definition of done for the initial CLI-driven store and the evidence provided by unit tests.
- **Planned v0.0.2 work:** [`docs/v0.0.2-plan.md`](v0.0.2-plan.md) outlines the next envelope iteration and runtime wiring.

## Roadmaps

- **Upcoming storage + transaction work:** [`FUTURE_UPDATES.md`](../FUTURE_UPDATES.md) captures the broader roadmap for durability, transactions, protocol, and operations.
- **Server buildout sequence:** [`server-roadmap.md`](server-roadmap.md) strings together schemas, runtime milestones, and operational tooling.

## How to navigate

- Looking for implementation detail? Start with `src/` alongside the specs above.
- Need to reproduce CI or run linters? Use the presets and helper targets in `CMakePresets.json` and check `ci-setup.md`.
- Want to extend the docs? Keep this index updated so new contributors can find the right entry point quickly.
