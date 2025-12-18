# ExecPlan: Storage Driver Cleanup and Implementation

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and `Outcomes & Retrospective` must be kept up to date as work proceeds. Maintain this file in accordance with `PLANS.md` at the repository root and treat it as the single source of truth for cleaning up and delivering storage drivers that match `MAIN_SPECIFICATION.md`.

## Purpose / Big Picture

Deliver storage drivers (pager, WAL, value log, checkpoint/superblock plumbing) that satisfy the durability and layout rules in `MAIN_SPECIFICATION.md` Section 6 and the WAL/redo rules in Section 7. When finished, a contributor should be able to start the database, perform SET/GET/DEL operations, observe WAL and value-log segments growing with CRC-protected records, crash and restart with redo-only recovery, and run maintenance (checkpoint + GC) without data loss.

## Progress

- [x] (2025-12-18 11:55Z) Documented current storage surface (pager, WAL manager, value log, simple store) and drafted this ExecPlan scoped to MAIN_SPECIFICATION.md Sections 6–7.
- [ ] (Pending) Lock in driver interfaces and invariants across pager/B+Tree/WAL/value log, including CRC and LSN contracts, before modifying code.
- [ ] (Pending) Implement segmented WAL with commit markers, CRC verification, and redo replay that rehydrates B+Tree pages and value-log references.
- [ ] (Pending) Implement value-log segmentation, pointer validation, and GC hooks tied to checkpoints and manifest metadata.
- [ ] (Pending) Wire checkpoints, superblock rotation, and manifest updates so recovery can select roots and LSN boundaries safely.
- [ ] (Pending) Land test coverage (unit + integration) and operational docs that prove crash safety, GC safety, and page integrity.

## Surprises & Discoveries

- None yet; populate with unexpected behaviors, tooling quirks, or data-format findings as implementation proceeds, including short evidence snippets (logs, test output) to justify follow-up changes.

## Decision Log

- Decision: Treat `src/storage/simple_store.*` as a temporary harness for exercising drivers; keep its API stable while refactoring internals so CLI/tests remain usable.  
  Rationale: Preserves a working surface for validation while enabling internal rewrites that align with the spec.  
  Date/Author: 2025-12-18 (assistant)

## Outcomes & Retrospective

No implementation work has started. Fill this section after each milestone to record what shipped, what gaps remain, and how behavior compares to the Purpose.

## Context and Orientation

The current storage surface includes `src/storage/pager/pager.*` (fixed-size pages with CRC but no free-list or page-type enforcement), `src/storage/wal/wal_manager.*` (single WAL file with FlatBuffer records, no segment rollover, no redo-to-B+Tree), `src/storage/vlog/value_log.*` (single-segment append/read with CRC per record, no GC or manifest links), and `src/storage/simple_store.*` (ties manifest/superblock, pager, value log, and B+Tree together). `MAIN_SPECIFICATION.md` Sections 6–7 define additional requirements: segmented WAL and value log, redo-only recovery with commit markers, page headers that carry type/LSN/CRC, hybrid inline/value-log storage, checkpointing, and TTL calibration stored in the superblock. No dedicated AGENTS apply beyond the root instructions, and `PLANS.md` governs ExecPlan format and maintenance expectations.

## Plan of Work

Milestone 1 (interfaces and invariants): Document and enforce driver contracts. Define pager header layout (id, type, lsn, CRC) and payload sizing in `src/storage/pager/pager.*`; specify how B+Tree records encode inline vs. value-log pointers in `src/storage/btree/` headers; pin WAL record shapes and expected FlatBuffers file identifiers. Update `meta::ManifestRecord` and `meta::SuperBlock` comments to capture page size, inline threshold, and TTL calibration so drivers can validate their inputs before touching disk.

Milestone 2 (pager cleanup and allocation model): Introduce a free-list or allocation map within `data.pages`, with page-type validation on read and write. Add CRC failure handling paths that surface actionable errors to callers. Ensure pager honors the write-ahead rule by requiring callers to provide the page’s last-applied LSN and refusing flushes when WAL is behind. Provide helper APIs to read/verify page headers without materializing payloads to support validators.

Milestone 3 (segmented WAL with redo pipeline): Replace the single WAL file in `src/storage/wal/` with segmented files (`wal-000001.log`, etc.) and an append path that writes size-prefixed FlatBuffer records with CRC. Implement `TxnBegin`, `Upsert`, `Tombstone`, `TxnCommit`, and `Checkpoint` record emission, with group-commit buffering and fsync discipline. Build a replay routine that scans from the last checkpoint, validates CRCs, stops at first corruption, builds the committed-TxnID set, and replays logical ops into the B+Tree and value log (using pager APIs to materialize pages).

Milestone 4 (value-log segmentation and GC hooks): Extend `src/storage/vlog/` to write segmented files with per-record CRCs and manifest-aware segment naming. Add pointer validation and length/offset bounds checks. Implement a GC scaffold that computes live references at checkpoint boundaries (using B+Tree traversal) and copies forward live records when the reclaimable ratio crosses a threshold, ensuring older segments are only deleted when safe relative to `last_checkpoint_lsn`.

Milestone 5 (checkpointing, superblocks, and TTL calibration): Implement a checkpoint scheduler that (a) sets a checkpoint LSN boundary, (b) flushes dirty pages whose page LSN ≤ checkpoint LSN after ensuring WAL is fsynced, and (c) writes the inactive superblock with updated root page id, last checkpoint LSN, and TTL calibration tuple `(wall_base, mono_base)` before flipping active generation. Ensure startup recovery selects the highest valid superblock generation and resumes WAL replay from `last_checkpoint_lsn`.

Milestone 6 (integration and testing): Update `SimpleStore` and any CLI/tooling to use the new driver APIs. Add unit tests under `tests/storage/` that cover pager CRC failures, WAL append/replay (including corruption stop), value-log pointer round-trips, and checkpoint selection. Add integration tests that simulate crash-and-restart by appending WAL/value-log records, truncating WAL tails, and verifying redo restores keys. Provide docs that explain directory layout and operational steps (`docs/`).

## Work Parallelization

This plan is intentionally sliced so multiple contributors can proceed with minimal coordination:

- Pager ownership: One worker focuses on `src/storage/pager/` and page header/free-list invariants. Dependency: agrees on header schema with WAL/B+Tree but can proceed using stub LSN values until redo lands.
- WAL ownership: Another worker handles `src/storage/wal/` segmentation, CRC, commit markers, and redo replay adapters that expose a stable API (`ReplayResult` with committed ops) to the storage layer. Dependency: expects pager APIs to read/write pages and B+Tree hooks to apply logical ops.
- Value-log ownership: A third worker enhances `src/storage/vlog/` with segmentation and GC scaffolding, exposing pointer validation helpers. Dependency: needs the inline-threshold and pointer schema defined in Milestone 1 but otherwise independent.
- Checkpoint/meta ownership: A fourth worker wires superblock/manifest updates, checkpoint scheduling, and TTL calibration persisted in `meta/`. Dependency: consumes WAL writer’s checkpoint markers and pager flush hooks; coordinates only on the contract for updating `last_checkpoint_lsn` and root page id.
- Test/validation ownership: A fifth worker writes tests under `tests/storage/` plus doc updates in `docs/`, consuming the stable APIs from other threads without needing internal details once interfaces are frozen in Milestone 1.

## Concrete Steps

Run commands from the repository root unless stated otherwise.

1) Prepare build trees (required by presets):  
    cmake --preset dev-debug  
    cmake --preset dev-debug-tidy

2) After code changes, enforce formatting and lint expectations:  
    cmake --build --preset dev-debug --target clang-format  
    cmake --build --preset dev-debug-tidy

3) Execute storage-specific tests (add as they are implemented):  
    ctest --preset dev-debug -R Storage  
    ctest --preset dev-debug -R Wal  
    ctest --preset dev-debug -R ValueLog

4) For crash/recovery drills, script reproducible runs that:  
    ./build/dev-debug/jubildb_server --config ./server.toml --workers 4  
    kill -9 <pid> after writes, then rerun the server and verify GETs return the last committed values.

## Validation and Acceptance

Implementation is acceptable when: (a) WAL segments are created with CRCs and survive replay through simulated crashes; (b) value-log pointers round-trip through B+Tree leaves, and GC never discards live data at checkpoint boundaries; (c) pager rejects corrupted pages and enforces page-type/LSN invariants; (d) checkpoints update superblocks with correct root and last checkpoint LSN; and (e) the documented tests in `Concrete Steps` pass consistently. Operational proof should include a manual run showing SET/DEL/GET across crash/restart with no data loss and shrinking/deleting value-log segments only after GC marks them safe.

## Idempotence and Recovery

All commands above are safe to rerun: CMake presets reuse the same build trees; pager/WAL/value-log initialization should be idempotent when directories already exist; recovery replay must tolerate partially written WAL/value-log tails and stop cleanly at the last valid CRC without advancing state incorrectly. When GC or checkpointing fails halfway, the plan requires leaving old segments/superblocks intact so reruns can proceed without manual cleanup.

## Artifacts and Notes

- Keep FlatBuffers schema identifiers in sync across WAL/value-log records and disk pages; regenerate code as part of the build if schemas change.  
- Document directory layout and segment naming in `docs/` to match `MAIN_SPECIFICATION.md` Section 6.6.  
- When revising this plan, add entries to `Decision Log`, `Progress`, and `Outcomes & Retrospective`, and append a short note to `Revision History` with the reason for change.

## Interfaces and Dependencies

Pager (`src/storage/pager/pager.*`): expose `Allocate(PageType) -> page_id`, `Read(page_id) -> Page{type,lsn,payload}`, `Write(Page)` that validates payload size and CRC, and `Sync()` that honors write-ahead ordering. Track `payload_size()` and header constants so B+Tree can size leaf/internal nodes correctly.

WAL (`src/storage/wal/`): define `WalManager::Append(const WalRecord&) -> Lsn`, `Flush()`, `Replay(from_lsn) -> {last_replayed, committed_ops}` with segmented file naming and CRC verification. `WalRecord` must cover `TxnBegin`, `TxnCommit`, `TxnAbort`, `Upsert` (inline or value-pointer), `Tombstone`, and optional `Checkpoint` markers.

Value log (`src/storage/vlog/`): provide `Append(vector<byte>) -> {SegmentPointer,length}`, `Read(SegmentPointer) -> optional<vector<byte>>`, `RunGcCycle()` that accepts live-pointer inventory and emits a compaction report. Segment pointers include `segment_id`, `offset`, and `length` with CRC validation on read.

Meta/checkpoint (`src/meta/` and `src/storage/checkpoint/`): superblocks must persist `{generation, root_page_id, last_checkpoint_lsn, wall_base, mono_base}` with dual files and CRCs. Checkpoint scheduler drives pager flushes and WAL truncation eligibility.

## Revision History

- 2025-12-18: Initial ExecPlan created to cover storage driver cleanup and implementation scope from `MAIN_SPECIFICATION.md`.
