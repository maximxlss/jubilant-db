# Server Implementation Roadmap

This roadmap sequences the work required to deliver the full server functionality outlined in the specifications. Each milestone includes a focused task stub with concrete steps and file paths.

## 1) Define stable schemas and manifest groundwork
Establish all FlatBuffers schemas and manifest structures so disk/wire formats are locked before engine code depends on them.

:::task-stub{title="Freeze FlatBuffers schemas + manifest metadata"}
* Draft `schemas/wire.fbs` with `TxnRequest/TxnResponse`, `KeyTableEntry`, op enums (GET/SET/DEL/ASSERT variants), `Value` union, and error codes.
* Draft `schemas/wal.fbs` capturing `TxnBegin/Upsert/Tombstone/Commit/Abort` records with LSNs and per-record CRC fields.
* Draft `schemas/disk.fbs` for MANIFEST, SUPERBLOCK (dual A/B), page headers, leaf/internal records (inline + vlog refs), and TTL calibration pair.
* Add CMake FlatBuffers codegen rules (`CMakeLists.txt`, `cmake/Modules` if needed) and hook generated headers into `src/` include paths.
* Create manifest reader/writer scaffolding in `src/meta/manifest.{h,cc}` enforcing major version compatibility and schema version hashes.:::

## 2) Pager and CRC-checked page I/O
Lay the groundwork for durable page access and caching.

:::task-stub{title="Implement pager with page types and CRC"}
* Add `src/storage/pager/page.h` for page header struct (id, type, LSN, CRC) and payload access helpers.
* Implement `src/storage/pager/pager.{h,cc}` to map/unmap fixed-size pages, verify CRC on read, and write with zeroed-CRC then finalize.
* Integrate LRU cache hooks (interface only) to be wired to unified cache later.
* Extend CMake to build pager module and add unit tests under `tests/pager_tests.cc` covering CRC validation and page type tagging.:::

## 3) B+Tree with inline values and leaf linkage
Provide the core index structure before layering WAL and value log.

:::task-stub{title="Build B+Tree core (inline values only)"}
* Define node layouts in `src/storage/btree/format.h` for internal/leaf pages (separator keys, child ids, next-leaf pointer, entry counts).
* Implement search/insert/delete in `src/storage/btree/tree.{h,cc}` using pager APIs; support splits/merges and tombstone markers.
* Add simple iterator for range scans (via leaf next pointers) to support TTL sweeper and validation.
* Unit tests in `tests/btree_tests.cc` for insert/find/delete ordering and tombstone semantics.:::

## 4) WAL writer/reader and recovery scan
Enable redo-only durability with segmented logs and CRC.

:::task-stub{title="Implement WAL append and redo replay"}
* In `src/storage/wal/`, add `writer.{h,cc}` for size-prefixed FlatBuffer records with CRC, segment rollover, and group-commit queue.
* Add `reader.{h,cc}` to stream records, stop at corruption, and expose LSN boundaries.
* Implement redo replay in `src/storage/wal/replay.{h,cc}`: gather committed txn IDs, apply logical ops to B+Tree, and truncate to last valid record.
* Wire CMake options for `dev-debug-tidy` preset to run WAL unit tests (`tests/wal_tests.cc`) simulating corruption boundaries.:::

## 5) Value log and GC scaffolding
Support large values with stable on-disk layout and future GC hooks.

:::task-stub{title="Add value log segments and references"}
* Define vlog record format in `schemas/disk.fbs` and generated helpers.
* Implement `src/storage/vlog/{segment.h,segment.cc}` for append/read with CRC, segment rollover, and pointer `{seg, offset, len, type}` creation.
* Extend B+Tree leaf value encoding to store vlog references when size exceeds inline threshold (read/write path updates).
* Add GC scaffolding in `src/storage/vlog/gc.{h,cc}` that marks segments eligible based on checkpointed LSN; allow no-op move initially.
* Tests in `tests/vlog_tests.cc` to verify round-trip read/write and handling of stale segments.:::

## 6) Unified cache and memory limits
Ensure predictable memory behavior across pager and value log buffers.

:::task-stub{title="Implement unified LRU cache"}
* Create `src/storage/cache/cache.{h,cc}` with configurable byte limit, LRU eviction, and handles for page/vlog blocks.
* Integrate with pager (page fetch pins cache entry) and optional decoded-value cache API for overlay reads.
* Add instrumentation counters (hits/misses/bytes) exposed via INFO later.
* Tests in `tests/cache_tests.cc` for eviction ordering and capacity accounting.:::

## 7) TTL handling and sweeper
Embed expiration semantics and background cleanup.

:::task-stub{title="Wire TTL evaluation and sweeper"}
* Implement TTL metadata encoding in leaf records (`src/storage/btree/format.h`) and value type.
* Add read-time check in B+Tree lookup to consult monotonic→wall calibration from `src/meta/superblock.{h,cc}`.
* Create sweeper in `src/storage/ttl/sweeper.{h,cc}` that scans leaves via iterator, materializes tombstones for expired items, and schedules via background thread.
* Configurable intervals from `src/config/config.{h,cc}` (TOML). Add tests using fake clock injection to validate expiration paths.:::

## 8) Superblock management and checkpoints
Support restart safety and WAL truncation eligibility.

:::task-stub{title="Add superblock A/B and fuzzy checkpointing"}
* Implement dual superblock read/write in `src/meta/superblock.{h,cc}` with generation, root page id, last checkpoint LSN, and TTL calibration pair plus CRC.
* Build checkpoint coordinator `src/storage/checkpoint/manager.{h,cc}`: establish checkpoint LSN, flush dirty pages respecting WAL LSN rule, update superblock, and mark old WAL segments deletable.
* Integrate with value-log GC trigger thresholds.
* Tests in `tests/checkpoint_tests.cc` verifying A/B selection, CRC failure fallback, and LSN advancement rules.:::

## 9) Lock manager and strict 2PL
Guarantee serializability for declared key sets.

:::task-stub{title="Implement per-key shared/exclusive locking"}
* Add `src/lock/lock_manager.{h,cc}` with hash-partitioned lock table, shared/exclusive modes, and canonical key ordering acquisition API.
* Provide RAII lock guard and timeout/error handling hooks for future metrics.
* Unit tests in `tests/lock_tests.cc` for concurrent acquisition ordering and upgrade attempts (disallowed by design).:::

## 10) Transaction planner and overlay execution
Translate wire ops into engine calls with read-your-writes.

:::task-stub{title="Build transaction executor with overlay"}
* In `src/txn/`, add `overlay.{h,cc}` to track per-txn reads/writes and tombstones.
* Implement `planner.{h,cc}` to validate key table, compute canonical lock order, and execute op list against overlay + storage APIs.
* Integrate WAL intent emission: buffer logical ops, hand to WAL writer, wait for group-commit ack, then apply to B+Tree.
* Support ASSERT ops using overlay-first reads and hash/type checks (hash algorithm from manifest).
* Tests in `tests/txn_tests.cc` for read-your-writes, assertion failures, and abort semantics.:::

## 11) Wire protocol server and request dispatch
Expose TCP/FlatBuffers interface with worker pool.

:::task-stub{title="Implement wire layer and server runtime"}
* Build `src/wire/framing.{h,cc}` for length-prefixed reads/writes and FlatBuffers verification of incoming frames.
* Add `src/wire/handlers.{h,cc}` mapping decoded requests to txn planner/wrapper for single-op transactions.
* Implement server runtime `src/server/server.{h,cc}`: blocking TCP accept, worker thread pool, connection loop, graceful shutdown hooks.
* Add basic logging configuration and TOML-driven listen address/port in `src/config/config.{h,cc}`.
* Integration tests (if feasible) under `tests/server_tests.cc` using loopback sockets and generated FlatBuffers clients.:::

## 12) Admin CLI (jubectl) and INFO endpoint
Provide operational tooling and introspection.

:::task-stub{title="Deliver INFO + jubectl commands"}
* Implement INFO handler in server to return text stats from cache, WAL, checkpoint, TTL sweeper, and format versions.
* Build `tools/jubectl/` subcommands (`info`, `checkpoint`, `repair`, `validate`, `dump-manifest`) using shared client library for FlatBuffers requests.
* Add validation walker `src/tools/validate` or similar to traverse pages and verify CRC/invariants.
* Tests for CLI argument parsing and mock server responses in `tools/jubectl/tests` or `tests/cli_tests.cc`.:::

## 13) Repair path on startup
Conservative recovery actions when corruption is detected.

:::task-stub{title="Implement auto-repair hooks"}
* Add `src/repair/repair.{h,cc}` to orchestrate WAL truncation to last valid record and dropping unreferenced tail value-log segments.
* Integrate into startup sequence (before accepting traffic) in `src/server/main.cc` or bootstrap module.
* Expose `jubectl repair` to trigger same flow manually.
* Tests simulating corrupted WAL/value-log tails and verifying database opens read-write after repair.:::

## 14) Configuration, logging, and build presets
Tie together runtime configuration and dev ergonomics.

:::task-stub{title="Finalize config loading and presets"}
* Expand `src/config/config.{h,cc}` to parse TOML for cache limits, checkpoint/sweeper intervals, GC thresholds, group-commit latency, listen address, and log level; provide defaults.
* Ensure `CMakePresets.json` builds `dev-debug` and `dev-debug-tidy` include schema codegen and tests.
* Add logging sink initialization (e.g., spdlog) in server bootstrap respecting config.:::

## 15) End-to-end testing and benchmarks
Validate correctness and performance envelopes.

:::task-stub{title="Add integration tests and perf harness"}
* Build integration test harness in `tests/integration/` that boots server against temp directory, exercises GET/SET/DEL, transactions with ASSERTs, TTL expiry, and recovery from crash (kill and reopen).
* Add microbenchmarks (if allowed) using Google Benchmark in `tests/benchmarks/` for GET/SET throughput and group-commit latency.
* Ensure CI configs run unit + integration suites under `cmake --preset dev-debug` and `dev-debug-tidy`.:::

This roadmap prioritizes format stability, storage fundamentals, durability, transaction logic, server exposure, and operational tooling to de-risk later milestones.
