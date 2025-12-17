# Long-range roadmap and dependency graph

This page maps a long-horizon plan for jubilant-db. It is intentionally structured as an iterative task graph: every milestone lists prerequisites, outcomes, and follow-on edges so we can reorder safely while still keeping a strict dependency spine. The goal is to reduce ambiguity when new contributors pick up work months from now.

## How to use this roadmap (iterative policy)

- **Iterate deliberately:** each wave below can be revisited. The graph is designed for multiple passes: start with enabling infrastructure, then harden durability, then scale horizontally, then operationalize.
- **Keep dependencies explicit:** tasks are numbered `Txx` with anchors. Dependencies link backward so it is obvious what must land before starting work.
- **Prove completion:** every task has tangible outcomes or acceptance checks instead of vague goals.

## Waves at a glance

1. **Foundations (T01–T05):** lock in formats, deterministic storage core, WAL, cache, and schema freeze.
2. **Transactional spine (T06–T10):** concurrency control, transaction planner, wire/runtime, and auth/bootstrap guards.
3. **Operational depth (T11–T17):** observability, TTL + repair, checkpoints, config reload, and admin tooling.
4. **Scale-out and resilience (T18–T25):** replication, tenancy, backups, schema evolution, compliance, connectors, managed footprint, and chaos drills.

## Task graph (numbered, linked, and blocked edges)

| # | Task | Dependencies | Outcomes and notes |
|---|------|--------------|--------------------|
| <a id="t01"></a>T01 | Format and manifest freeze | None | Finalize FlatBuffers schemas (`wire`, `wal`, `disk`) plus manifest fields (version hash, hashing algo, TTL calibration). Land codegen wiring in CMake presets so all builds share generated headers. |
| <a id="t02"></a>T02 | Pager + CRC-checked pages | [T01](#t01) | Implement page header struct, read/write with CRC verification, and mapping API. Provide unit tests for CRC rejection and type tagging. |
| <a id="t03"></a>T03 | Write-ahead log with redo replay | [T02](#t02) | WAL writer/reader with size-prefix + CRC, segment rollover, and redo replay to B+Tree skeleton. Integration test that truncates on corruption. |
| <a id="t04"></a>T04 | B+Tree core with inline values | [T02](#t02), [T03](#t03) | Node layouts, search/insert/delete, splits/merges, and leaf chaining iterator. Inline-only values; no GC yet. |
| <a id="t05"></a>T05 | Unified cache + memory guardrails | [T02](#t02), [T04](#t04) | Shared LRU cache for pages/value blocks with byte budgeting, eviction metrics, and pager hooks. Benchmarks for hit/miss behavior. |
| <a id="t06"></a>T06 | Lock manager (strict 2PL) | [T04](#t04) | Hash-partitioned lock table with shared/exclusive modes and canonical key ordering. Tests for fairness and upgrade rejection. |
| <a id="t07"></a>T07 | Transaction planner + overlay | [T04](#t04), [T06](#t06) | Overlay for read-your-writes, ASSERT support, and lock ordering. Buffers logical ops for WAL handoff. Coverage for abort paths. |
| <a id="t08"></a>T08 | Value log + GC scaffolding | [T04](#t04), [T05](#t05), [T07](#t07) | Append-only value segments with CRC, references in B+Tree leaves, and GC markers keyed off checkpointed LSN. Includes relocation hooks even if initially no-op. |
| <a id="t09"></a>T09 | Wire server runtime + framing | [T07](#t07) | TCP accept loop, worker pool, length-prefixed framing (FlatBuffers verification), request dispatch to transaction planner. Config-driven listen options. |
| <a id="t10"></a>T10 | Authn/authz + network hardening | [T09](#t09) | TLS termination, optional mutual TLS, static API tokens or local ACL map. Reject unauthenticated requests and expose INFO output without secrets. |
| <a id="t11"></a>T11 | Observability spine | [T09](#t09) | Structured logging, metrics surface (cache hits, WAL lag, checkpoint age), and trace hooks. Add INFO endpoint fields for each counter. |
| <a id="t12"></a>T12 | TTL evaluation + sweeper | [T04](#t04), [T05](#t05), [T07](#t07) | TTL metadata encoding, read-time validation using calibration pair, background sweeper producing tombstones, and tests with fake clock. |
| <a id="t13"></a>T13 | Checkpointing + superblock A/B | [T03](#t03), [T08](#t08) | Fuzzy checkpoint coordinator, dual superblock with CRC, WAL truncation eligibility, and value-log GC triggers. |
| <a id="t14"></a>T14 | Repair + startup recovery guardrails | [T03](#t03), [T13](#t13) | Detect corrupted WAL/vlog tails, truncate safely, and reopen read-write. CLI command to invoke repair manually. |
| <a id="t15"></a>T15 | Config system + live reload | [T09](#t09), [T11](#t11) | TOML-backed config loader with defaults, validation, and optional SIGHUP reload for tunables (cache size, worker pool, sweep interval). |
| <a id="t16"></a>T16 | Admin CLI (jubectl) deepening | [T09](#t09), [T11](#t11), [T14](#t14) | Commands for `info`, `checkpoint`, `repair`, `validate`, and `dump-manifest`. Uses shared client lib; includes offline validation walker. |
| <a id="t17"></a>T17 | End-to-end validation + perf harness | [T09](#t09), [T12](#t12), [T14](#t14) | Integration suite booting server, covering txn flows, TTL expiry, crash+recovery, and perf microbenchmarks for GET/SET and group-commit latency. |
| <a id="t18"></a>T18 | Replication (follower catch-up) | [T13](#t13), [T17](#t17) | WAL shipping to followers, snapshot handoff, and deterministic log-apply cursor. Adds consistency metric (replication lag). |
| <a id="t19"></a>T19 | Active failover + leader election | [T18](#t18) | Leader election (e.g., Raft-style) with fencing tokens, write redirection, and split-brain detection. Follower promotion drills. |
| <a id="t20"></a>T20 | Multi-tenancy + quotas | [T05](#t05), [T15](#t15), [T18](#t18) | Namespaced manifests, per-tenant caches/quotas, and rate limiting in server front-door. Tenant-scoped INFO outputs. |
| <a id="t21"></a>T21 | Backup/restore + point-in-time recovery | [T13](#t13), [T18](#t18) | Consistent snapshots with WAL cursor, remote object store uploads, PITR by replaying WAL to target LSN. Dry-run restore validator. |
| <a id="t22"></a>T22 | Online schema evolution hooks | [T01](#t01), [T13](#t13) | Versioned schemas with compatibility gates, rolling bump process (quiesce, dual-read/write), and manifest-driven feature flags. |
| <a id="t23"></a>T23 | Ecosystem connectors | [T09](#t09), [T11](#t11), [T18](#t18) | Language clients (Go/Rust/Python) with idiomatic retries, connection pooling, and observability. CDC sink or Kafka connector for change streaming. |
| <a id="t24"></a>T24 | Managed footprint + packaging | [T15](#t15), [T18](#t18), [T21](#t21) | Container images, IaC modules, Helm chart, and zero-downtime rolling upgrade recipe tied to checkpoint/PITR safeguards. |
| <a id="t25"></a>T25 | Chaos, drills, and compliance hardening | [T19](#t19), [T21](#t21), [T24](#t24) | Fault-injection suite (disk corruption, partial writes, network partitions), audit logging, encryption at rest, and compliance checklist (SOC2-style). |

### Task elaborations and acceptance checkpoints

- **T01–T05 (Foundations):**
  - Acceptance hinges on deterministic storage artifacts: binary compatibility tests for FlatBuffers schemas (golden files), CRC failure tests for pager pages, and cache eviction telemetry proving bounded memory. Incorporate schema hash assertions in manifest load to detect drift.
  - Explicitly document page header fields and WAL record envelopes in `TECH_SPECIFICATION.md` once stabilized to prevent divergent interpretations.
- **T06–T10 (Transactional spine):**
  - Lock manager must demonstrate deadlock avoidance via canonical ordering and time-bound acquisition tests. Overlay should keep shadow versions for ASSERT ops and expose rollback counters.
  - Wire runtime readiness requires soak tests with mixed request types, TLS off/on toggles, and load-shedding behavior when worker backlog exceeds a threshold.
- **T11–T17 (Operational depth):**
  - Observability exit criteria include INFO surface parity with internal metrics (no hidden counters) and log redaction rules for keys/values.
  - Repair flows must simulate corrupted WAL tails and stale value-log segments, with before/after manifests recorded for auditability.
  - Integration harness should produce reproducible traces (seeded random workloads) and benchmark outputs stored under `build/<preset>/reports` for historical comparison.
- **T18–T25 (Scale + resilience):**
  - Replication/failover requires fencing tokens and monotonic term/epoch tracking; add failure-injection tests for log divergence and delayed snapshots.
  - Backup/PITR must prove correctness by restoring to multiple LSNs and comparing page-level hashes against the origin node.
  - Compliance milestone is not just checklists: require audit log retention tests, crypto boundary verification, and chaos scenarios covering network partitions plus disk-full events.

### Critical paths and parallel tracks

- **Critical durability spine:** [T01](#t01) → [T02](#t02) → [T03](#t03) → [T04](#t04) → [T07](#t07) → [T09](#t09) → [T13](#t13) → [T18](#t18) → [T19](#t19).
- **Operational observability path:** [T09](#t09) → [T11](#t11) → [T15](#t15) → [T16](#t16) → [T17](#t17) → [T24](#t24).
- **Data lifecycle path:** [T04](#t04) → [T08](#t08) → [T12](#t12) → [T13](#t13) → [T21](#t21).

### Iteration guidance

1. **Pass 1 (stabilize storage):** Complete [T01](#t01)–[T05](#t05) to ensure disk and WAL invariants are deterministic before widening the surface area.
2. **Pass 2 (unlock transactions and networking):** Advance [T06](#t06)–[T11](#t11) to make the system usable end-to-end with visibility.
3. **Pass 3 (operational hardening):** Deliver [T12](#t12)–[T17](#t17) to prove recoverability and observability under load.
4. **Pass 4 (scale and resilience):** Execute [T18](#t18)–[T21](#t21) to gain high availability and data safety beyond a single node.
5. **Pass 5 (ecosystem + compliance):** Finish [T22](#t22)–[T25](#t25) to make the product enterprise-ready and manageable as a service.

### Notes for future revisions

- Revisit dependency arrows if we change on-disk formats or add new invariants (e.g., new value types). The graph favors strict prerequisites; if we decouple an item, note the removal explicitly.
- Keep every task acceptance-oriented (tests, metrics, drills) so the roadmap remains actionable rather than aspirational.
- When splitting tasks for parallel teams, ensure shared code paths (pager, WAL, lock manager) stay feature-flagged until the critical path validates them together.
