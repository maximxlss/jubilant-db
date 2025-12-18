# Roadmap

This roadmap folds the near-term and long-range plans into one place so contributors can see what is done, what is next, and how the work threads together. It is intentionally aligned with the product direction captured in [`MAIN_SPECIFICATION.md`](../MAIN_SPECIFICATION.md) so the outside-facing README and the delivery path stay consistent.

## Near-term spine

The items below are the critical spine that keeps durability and transaction semantics coherent while the release matures. They replace the fragmented server/durability roadmaps and should be treated as the single source of truth.

1. **Format + manifest freeze**
   - Finalize FlatBuffers schemas for wire/WAL/disk and record hashes in the manifest.
   - Wire schema codegen into CMake presets so every build shares generated headers.
2. **Pager, WAL, and B+Tree resilience**
   - CRC-tagged page IO with type headers, WAL writer/reader + redo replay, and B+Tree split/merge with iterator support.
   - Add segment rollover, fsync discipline, and cache hooks with byte budgeting.
3. **Value handling and TTL**
   - Value-log segments with CRC, references in leaves, and GC scaffolding tied to checkpoints.
   - TTL encoding + calibration, read-time enforcement, and a sweeper that emits tombstones.
4. **Checkpointing + recovery guardrails**
   - Dual superblocks with generation selection, fuzzy checkpoints that gate WAL truncation, and repair flows for corrupted WAL/value-log tails.
5. **Transactions and locking**
   - Strict 2PL over canonical key ordering, overlay planner with ASSERT support, and WAL handoff before page mutation.
6. **Wire/runtime maturity**
   - TCP listener with framing verification, worker pool backpressure, structured logging, and TOML-driven configuration for listen address, worker count, cache limits, and sweeper/checkpoint intervals.
7. **Operational tooling**
   - INFO surface for cache/WAL/checkpoint/TTL metrics, deep `jubectl` commands (info/checkpoint/repair/validate), offline validation walker, and packaging targets.

## Long-range evolution

The long-horizon work focuses on resilience and operability once the single-node story is solid and the parallel execution path is fully exercised in production-like settings.

- **Resilience and HA:** replication with snapshot handoff, leader election with fencing, and PITR backups with verifiable restores.
- **Richer data shapes:** typed collections (lists, sets, hashes) and batched operations once the core KV invariants are proven stable.
- **Access controls and governance:** authentication, roles/ACLs, audit logging, and policy hooks so multiple teams can share a node safely.
- **Performance and parallelism:** adaptive worker scheduling, parallel background maintenance (checkpoint/GC), and throughput drills that stress strict 2PL under load.
- **Scale and tenancy:** namespaced manifests, quotas, rate limiting, and connection pooling in clients plus CDC/connectors.
- **Operational rigor:** config reloads, chaos drills (disk corruption, partitions), encryption at rest, and compliance-grade observability.
- **Distribution:** container images, IaC/Helm modules, and rolling-upgrade guidance tied to checkpoint/PITR safeguards.

## How to read this roadmap

- Treat the list above as the single roadmap. If you need milestone-level steps, consult the v0.0.2 ExecPlan; otherwise update this file directly when priorities shift.
- Keep dependencies in mind: durability → transactions → wire/runtime → operations → scale. Do not start downstream items without proving upstream invariants with tests.
- When splitting work, retain acceptance checks (tests, metrics, drills) in each PR so this document remains actionable rather than aspirational.
