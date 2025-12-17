# Future updates roadmap

This roadmap threads together the next waves of work so the project can grow into a production-ready single-node database. Each theme should land with tests, observability, and operational guidance.

## Durability and recovery

- Harden MANIFEST persistence (fsync, monotonic generation checks) and extend dual-superblock rotation to bump every checkpoint while rejecting mixed generations on open.
- Implement WAL segment rollover, durable fsync checkpoints, and expose a durable LSN once persisted.
- Add a checkpointer that flushes dirty pages, records a checkpoint LSN in the superblock, and prunes WAL segments that are fully checkpointed.
- Sequence crash recovery: MANIFEST → superblock selection → WAL redo → checkpoint replay → value-log reconciliation.

## Storage engine completeness

- Finish B+Tree split/merge and sibling redistribution; enforce UTF-8 ordering and stable root management.
- Route large values through the value log, store value-log pointers in leaves, and support GC/compaction.
- Add page-level CRCs and type tags, surfacing corruption detection hooks for repair workflows.
- Build a unified cache (likely LRU) that fronts pager reads and participates in eviction during checkpoints.

## TTL and time calibration

- Persist `(wall_now, mono_now)` calibration in the superblock; compute wall-clock expiry using monotonic deltas.
- Enforce TTL checks on reads and return TTL metadata alongside values.
- Add a sweeper to expire keys, emit tombstones, and trigger compaction; expose drift metrics.

## Transactions and locking

- Finalize the transaction executor with strict 2PL over declared key sets, canonical key ordering, and deadlock-free acquisition.
- Implement ASSERT operations (exists/not-exists/type/int/hash) using stable hashes recorded in the MANIFEST.
- Add rollback that discards overlays, unlocks keys, and avoids storage mutation on abort.
- Integrate WAL with transactions so writes are logged before page/value-log updates; support group-commit timers and size thresholds.

## Protocol, server runtime, and CLI parity

- Finalize FlatBuffers wire schemas with file identifiers and versioning; generate code in the build.
- Complete the blocking TCP server that parses frames, executes transactions, and emits responses with graceful shutdown semantics.
- Expand `jubectl` to drive init/CRUD/TTL, `INFO`, validation/repair, and checkpoint triggers against the server.
- Add configuration loading (TOML) for thread counts, page sizing, value-log paths, WAL segment sizing, and fsync policies.

## Observability, validation, and repair

- Implement `INFO` plumbing to expose cache stats, LSNs, checkpoint state, WAL segment counts, and TTL sweeper status.
- Build offline `jubectl validate/repair` that scans MANIFEST, superblocks, page CRCs, and WAL continuity; reconstruct B+Tree/value-log pointers when possible.
- Emit structured logs for recovery steps, WAL roll, checkpoint completion, and repair actions.

## Testing, benchmarks, and release readiness

- Add crash-recovery matrices, transaction isolation tests, and property/fuzz coverage for FlatBuffer decoding and B+Tree invariants.
- Provide microbenchmarks for CRUD latency, WAL throughput, cache hit rates, and value-log fetch costs under varying fsync policies.
- Document release checklists covering schema bumps, compatibility gates, and operational runbooks.
