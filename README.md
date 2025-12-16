# Jubilant DB

Jubilant DB is a single-node, hybrid memory+disk key–value store focused on clear semantics, durability primitives, and approachable tooling. The project uses FlatBuffers on the wire and on disk, relies on a B+Tree paired with a WAL and value log, and targets strict two-phase locking for transactions.

Key entry points for the project documents:

* `MAIN_SPECIFICATION.md` — end-to-end product and storage specification for Jubilant DB v1.0.
* `TECH_SPECIFICATION.md` — recommended stack and tooling for the implementation.
* `FIRST_STEPS.md` — milestone-oriented checklist for bootstrapping the codebase.

## Documentation map

The docs are intentionally segmented so contributors can find the right level of detail quickly:

* **Product & storage spec:** See `MAIN_SPECIFICATION.md` for semantics, protocol, and storage rules.
* **Engineering stack:** See `TECH_SPECIFICATION.md` for language, build, and dependency decisions.
* **Execution plan:** See `FIRST_STEPS.md` for the first implementation milestones and acceptance criteria.
* **Repo standards:** Conventional commits and CI expectations are summarized at the end of `MAIN_SPECIFICATION.md`.

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

## Development readiness (v0 scaffolding)

This repository currently contains scaffolding for the pager, schema generation, CLI entry points, and test harness wiring. The next steps should focus on:

* Filling in storage engine components (manifest/superblock IO, WAL writer/reader, B+Tree persistence).
* Expanding `jubectl` commands beyond `init` to drive CRUD flows.
* Adding unit and integration tests aligned with `FIRST_STEPS.md`.

Active scaffolding modules are wired into the build so they can evolve incrementally:

* Storage: pager, B+Tree in-memory shim, WAL manager, value log pointer tracking, checkpoint placeholder.
* Meta: manifest and superblock placeholders.
* Runtime: basic lock manager, transaction context overlay, and server lifecycle shell.
* Config: default loader with a TOML-ready hook.

Every component should favor small, testable interfaces so that future work can proceed in a TDD-friendly manner.

## CMake presets for development

The repository ships with CMake presets to keep day-to-day workflows short and reproducible (requires CMake 3.25+ and Ninja):

* Configure a Debug tree with tests enabled and compile commands exported:

  ```sh
  cmake --preset dev-debug
  ```

* Build either everything or just the tests for the configured tree:

  ```sh
  cmake --build --preset dev-debug
  cmake --build --preset dev-debug-tests
  ```

* Run the unit test suite (ctest uses the configured Debug tree automatically):

  ```sh
  ctest --preset dev-debug
  ```

Release-tuned variants are available via the `dev-release` presets when you want to check optimizer-sensitive behavior.

## License

Jubilant DB is available under the [MIT License](LICENSE), encouraging reuse and contribution without restrictive terms.
