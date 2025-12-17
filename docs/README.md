# Jubilant DB documentation map

This index standardizes where to find design, implementation, and roadmap material for Jubilant DB.

## Specifications

* **Product + storage spec:** [`MAIN_SPECIFICATION.md`](../MAIN_SPECIFICATION.md) captures semantics, protocol, storage layout, and operational rules for v1.0.
* **Technical stack:** [`TECH_SPECIFICATION.md`](../TECH_SPECIFICATION.md) documents the toolchain, build system, and dependencies.
* **Wire protocol (v0.0.2 draft):** [`txn-wire-v0.0.2.md`](txn-wire-v0.0.2.md) details the length-prefixed JSON envelope for `set/get/del` transactions.

## Milestones and status

* **Current milestone (v0.0.1):** [`FIRST_STEPS.md`](../FIRST_STEPS.md) defines the acceptance criteria for the initial CLI-driven store. The implemented `jubectl init/set/get/del` flows are validated by unit tests in `tests/` (SimpleStore persistence, pager IO, B+Tree correctness, and metadata scaffolding). Observability now includes `jubectl stats`/`validate` that surface manifest/superblock metadata and checkpoint state for quick health checks.
* **Test evidence:** Run `ctest --preset dev-debug` after configuring via `cmake --preset dev-debug` to exercise the v0.0.1 coverage.
* **Server runtime scaffolding:** [`server-runtime.md`](server-runtime.md) documents the transaction receiver + worker pool wiring that will back the wire protocol in upcoming milestones.

## Roadmap

* **Near-term steps:** The README surface roadmap outlines the next few iterations beyond v0.0.1.
* **Longer horizon:** [`FUTURE_UPDATES.md`](../FUTURE_UPDATES.md) tracks deeper durability, transaction, and protocol work.
* **Full server buildout:** [`docs/server-roadmap.md`](server-roadmap.md) sequences milestones from schemas through wire protocol and operational tooling.

## Contribution and tooling

* **Build & CI:** Day-to-day build/test commands live in the top-level [`README.md`](../README.md); CI mirrors those presets and enforces `clang-format`/`clang-tidy`.
* **Developer workflow:** Refer to [`CONTRIBUTING.md`](../CONTRIBUTING.md) for conventions, commit expectations, and environment setup tips.
