# Jubilant DB

Jubilant DB is a single-node, hybrid memory+disk key–value store focused on clear semantics, durability primitives, and approachable tooling. The project uses FlatBuffers on the wire and on disk, relies on a B+Tree paired with a WAL and value log, and targets strict two-phase locking for transactions.

Key entry points for the project documents:

* `MAIN_SPECIFICATION.md` — end-to-end product and storage specification for Jubilant DB v1.0.
* `TECH_SPECIFICATION.md` — recommended stack and tooling for the implementation.
* `FIRST_STEPS.md` — milestone-oriented checklist for bootstrapping the codebase.

## CLI

The repository standardizes on **`jubectl`** as the command-line interface for local administration and end-to-end exercises. The initial command set includes initialization, CRUD operations, stats, and validation/repair flows as defined in the specifications.

## Repository layout (planned)

The codebase is organized to keep storage, transaction, and tooling concerns isolated. The following directories are pre-created to anchor future work:

* `schemas/` — FlatBuffers definitions for wire, WAL, and disk formats.
* `src/` — core engine sources (pager, B+Tree, WAL, TTL, cache, server runtime).
* `tools/jubectl/` — CLI sources for `jubectl`.
* `tests/` — unit and integration tests, including persistence and correctness coverage.
* `cmake/` — shared CMake helpers/toolchain settings.

With these seeds in place, you can begin wiring up the build (CMake targets for the server, `jubectl`, and tests), then iterate through the FIRST_STEPS milestones.
