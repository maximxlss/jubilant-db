# Future updates roadmap

This roadmap broadens the coverage across storage, transactions, durability, protocol, and tooling so every unfinished element in the specs has a landing zone.

1. **Durability + recovery (MANIFEST, superblock, WAL, checkpoints)**
   * Persist MANIFEST with size-prefix + file identifier and fsync; introduce a monotonic manifest generation to reject stale loads.
   * Dual-superblock rotation with CRC validation and generation bumps on every checkpoint; reject mixed generations on open per spec.
   * Implement WAL append/flush with size-prefixed FlatBuffers, CRC checks, and segment roll-over; expose durable LSN after fsync.
   * Wire Checkpointer to flush dirty pages, cut a checkpoint LSN into the superblock, and prune WAL segments that are fully checkpointed.
   * Add crash-recovery sequencing: MANIFEST -> superblock select -> WAL redo -> checkpoint replay -> value-log reconciliation.

2. **Storage engine completeness (pager, B+Tree, value log)**
   * Finish B+Tree split/merge, sibling redistribution, and root management; enforce UTF-8 keys and stable ordering.
   * Thread inline threshold from MANIFEST to route large values to the value log; store value-log pointers in leaves and support fetching/GC.
   * Add page-level CRCs and type tags to `Pager`; surface corruption detection hooks for repair.
   * Implement compaction for inline pages + value log to shrink overwrite churn; include tombstone coalescing rules.
   * Build LRU/unified cache that fronts pager reads and participates in checkpoint eviction decisions.

3. **TTL and time calibration**
   * Persist `(wall_now, mono_now)` calibration in the superblock and apply monotonic deltas to compute wall-clock expiry per MAIN_SPECIFICATION.
   * Enforce TTL checks on `Get`/transactional reads and return TTL metadata alongside values.
   * Add sweeper to expire keys, emit tombstones, and trigger compaction; expose drift metrics.

4. **Transactions, locking, and overlay correctness**
   * Finalize transaction executor with strict 2PL over declared key sets (shared vs exclusive), canonical sort order, and deadlock-free acquisition.
   * Implement ASSERT operations (exists/not-exists/type/int/hash) using stable hash recorded in MANIFEST; ensure overlay honors read-your-writes.
   * Add transaction rollback path that discards overlay, unlocks keys, and never mutates storage on abort.
   * Integrate WAL with transactions for write-ahead logging before page/value-log updates; support group commit timer + size thresholds.

5. **Protocol, server runtime, and CLI parity**
   * Finalize FlatBuffers wire schemas (file identifiers, versioning) for requests/responses and WAL; generate code in build.
   * Finish blocking TCP server that parses frames, executes txns, and emits responses; add graceful shutdown + signal handling.
   * Expand `jubectl` to cover init, CRUD, TTL maintenance, `INFO`, validation/repair, and checkpoint triggers.
   * Add config loader (TOML) for thread counts, page size, value-log paths, WAL segment sizing, and fsync policies.

6. **Observability, validation, and repair**
   * Implement `INFO` plumbing to expose cache stats, LSNs, checkpoint state, WAL segment counts, and TTL sweeper status.
   * Add offline `jubectl validate/repair` that scans MANIFEST, superblocks, page CRCs, and WAL continuity; reconstruct B+Tree/value-log pointers when possible.
   * Emit structured logs for recovery steps, WAL segment roll, checkpoint completion, and repair actions.

7. **Testing, benchmarks, and release readiness**
   * Build unit/integration suites for pager IO, WAL redo, TTL expiry, transaction isolation, and crash-recovery matrix (clean vs dirty shutdown).
   * Add fuzz/property tests for FlatBuffer decoding and B+Tree invariants; include checksum corruption cases.
   * Provide microbenchmarks for CRUD latency, WAL throughput, cache hit rates, and value-log fetch costs; run under both fsync-on-commit and relaxed modes.
   * Document release checklist covering manifest schema bumps, compatibility gates, and operational runbooks.
