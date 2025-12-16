# Jubilant DB v1.0 Specification

A single-node, hybrid memory+disk key–value database with a Redis-like feel (simple commands + transactions), built around a **C-compatible binary protocol**, **FlatBuffers** for wire + disk structures, **serializable transactions (strict 2PL)**, and a **B+Tree + WAL + value-log** storage engine.

This spec is “ready to build”: it nails down semantics, file layout, recovery rules, concurrency model, and developer workflow.

---

## 1. Scope

### In scope (v1)

* Single database per server instance.
* Keys are user-provided **UTF-8 strings** (invalid UTF-8 is rejected).
* Values: `Bytes`, `String(UTF-8)`, `Int64`.
* Serializable multi-key transactions (strict 2PL).
* TTL per key + record metadata.
* Hybrid storage: small values inline in B+Tree leaves, large values in value log.
* WAL with group commit; redo-only recovery.
* Auto repair on open (conservative).
* LRU cache (unified).
* Admin CLI (`jubectl`) and `INFO` introspection.

### Out of scope (v1)

* Redis protocol compatibility.
* External plugin/modules system (internal modularity only).
* Advanced data structures (lists, sets, hashes, etc.).
* Authentication/ACL.
* Replication/cluster.

---

## 2. External API Semantics

### 2.1 Keys

* **Key type:** UTF-8 string.
* **Comparison/order:** bytewise lexicographic on UTF-8 bytes (no collation/locale).
* **Uniqueness:** a key maps to at most one record at a time (or tombstone).

### 2.2 Value types (v1)

* `BYTES`: arbitrary bytes.
* `STRING`: must be valid UTF-8.
* `INT64`: signed 64-bit.

### 2.3 Core operations (non-transactional)

* `GET(key) -> (Missing | Value)`
* `SET(key, value) -> OK` (unconditional overwrite)
* `DEL(key) -> (0|1)` (tombstones key; cleanup later)
* `INFO() -> text`

Non-transactional operations are implemented as implicit single-op transactions (acquire needed lock(s), apply, commit).

### 2.4 Transactions (single-shot frame; non-interactive)

A transaction is exactly **one request frame** containing:

1. a **key intern table**: `{ id -> (mode, key_utf8_bytes) }`, where `mode ∈ {R, RW}`
2. an **operation list** referencing key IDs
3. optional transaction flags (e.g., desired durability class in future—v1 has none)

#### Rules

* If an op references an unknown ID → error, abort txn.
* If an op writes a key declared `R` → error, abort txn.
* If any ASSERT fails → abort txn (no partial effects).
* Strict 2PL: locks held until COMMIT/ABORT completes.

#### Read-your-writes

* Each txn maintains an in-memory **overlay**:

  * reads consult overlay first
  * writes update overlay
* The storage engine is mutated only after locks are acquired and the txn executes successfully (still subject to WAL protocol).

### 2.5 “Conditionals” inside transactions (ASSERT ops)

To justify transactions without CAS/conditional SET, v1 includes ASSERT operations:

Minimum set:

* `ASSERT_EXISTS(id)`
* `ASSERT_NOT_EXISTS(id)`
* `ASSERT_TYPE(id, {Missing,Tombstone,Bytes,String,Int64})`
* `ASSERT_INT_EQ(id, int64_expected)`
* `ASSERT_BYTES_HASH_EQ(id, hash_expected)`
* `ASSERT_STRING_HASH_EQ(id, hash_expected)`

Notes:

* Hash is a fixed algorithm chosen in implementation and recorded in manifest (so it’s format-stable).
* Assertions read from the txn view (overlay-first, then storage; TTL applies).

---

## 3. TTL and Time

### 3.1 Time source

* TTL evaluation uses a **monotonic clock**.
* The DB stores TTL as “expires at” in monotonic time units consistent within a process lifetime. (On restart, TTL semantics require a persisted mapping—see below.)

**Important constraint:** A monotonic clock is not naturally restart-stable. Therefore the on-disk TTL encoding must be defined as:

* **Option used in v1:** store expiration as **wall-clock Unix time** *and* evaluate TTL using a monotonic clock by maintaining a persisted **monotonic↔wall calibration** at startup.

  * On startup, record `(wall_now, mono_now)` in superblock.
  * To evaluate: `wall_estimate = wall_base + (mono_now - mono_base)`.
  * TTL compares against `wall_estimate`.
    This preserves your requirement (“using a monotonic clock”) while keeping TTL meaningful across restarts.

### 3.2 TTL behavior

* **Read-time:** if expired → treat as `Missing` (logically deleted).
* **Sweeper:** periodically scans and materializes tombstones/cleanup candidates to enable compaction/GC.

---

## 4. Concurrency Model

### 4.1 Server threading

* Blocking TCP sockets.
* Thread pool processes requests. A single request can execute on any worker thread.

### 4.2 Locks and serializability

* **Strict 2PL** with predeclared key sets.
* Lock acquisition:

  * Server sorts keys by canonical ordering and acquires locks in that order:

    * `R` → shared lock
    * `RW` → exclusive lock
* Deadlocks are avoided by canonical acquisition ordering (no deadlock detector).

### 4.3 No explicit limits

* v1 does **not** impose protocol-level limits (frame size, key count, ops count).
* This is explicitly a “trusted deployment” posture. If later needed, limits can be added as hardening without changing semantics.

---

## 5. Protocol

### 5.1 Transport and framing

* TCP.
* Each message is: `u32 little-endian length` followed by payload bytes.
* Payload is a FlatBuffers root object with a schema identifier/file-id.

### 5.2 Encoding

* FlatBuffers used for:

  * wire requests/responses
  * WAL records
  * disk records/pages (as defined below)
* Little-endian on disk and wire.

### 5.3 Transaction request shape (wire)

* `TxnRequest { KeyTable, OpList }`
* `KeyTableEntry { id: u32, mode: R|RW, key_bytes: [ubyte] }`
* Ops reference `id` only; keys never appear elsewhere in the frame.

---

## 6. Storage Engine

### 6.1 Filesystem layout (directory)

Database directory contains:

* `MANIFEST` (FlatBuffer; minimal immutable-ish metadata)
* `SUPERBLOCK_A`, `SUPERBLOCK_B` (dual superblock w/ generation + CRC)
* `data.pages` (fixed-size pages; page size chosen at creation)
* `wal-000001.log`, `wal-000002.log`, … (segmented WAL)
* `vlog-000001.seg`, `vlog-000002.seg`, … (segmented value log)

### 6.2 Manifest

Contains:

* format major/minor version
* schema version identifiers (wire/wal/disk schema hashes or version numbers)
* DB UUID
* created-at timestamp
* page size
* inline value threshold (bytes)
* hash algorithm identifier for ASSERT_HASH ops

Strict versioning:

* Major mismatch → refuse open (no auto-migrate, no repair).

### 6.3 Superblocks (A/B)

* Each superblock stores:

  * generation counter
  * pointer to current B+Tree root page id
  * last checkpoint LSN
  * `(wall_base, mono_base)` calibration pair for TTL evaluation
  * CRC checksum

On update:

* write inactive superblock with incremented generation + CRC, then atomically replace.
* On open:

  * read both, pick the highest generation with valid CRC.

### 6.4 Page file: `data.pages`

* Fixed-size pages.
* Each page header includes:

  * page id
  * page type
  * page LSN (or “page_last_applied_lsn”)
  * CRC checksum over the page

B+Tree structure:

* Internal pages: separator keys + child page ids.
* Leaf pages: key bytes → value reference + metadata (incl. TTL/flags/revision).

### 6.5 Hybrid value storage

* If encoded value size ≤ inline threshold → stored inline in leaf record.
* Else stored in value log; leaf stores pointer `{segment_id, offset, length}` plus checksum/hash if desired (optional; page CRC already exists).

### 6.6 Value log

* Segmented append-only, segmented similarly to WAL.
* Each record is a size-prefixed FlatBuffer with identifier + CRC.
* GC triggers:

  * periodically
  * and when reclaimable ratio exceeds configured threshold

GC semantics:

* Live-ness determined by B+Tree references at a safe checkpoint boundary.
* GC never breaks crash safety; it operates on segments older than a safe LSN.

### 6.7 Deletions

* `DEL` creates a tombstone record in the B+Tree.
* Tombstones are cleaned during background maintenance (checkpoint/GC), not necessarily immediately.

---

## 7. WAL, Commit, Checkpointing, Recovery

### 7.1 WAL structure

* Segmented WAL files, append-only.
* Each WAL record is:

  * size prefix (u32)
  * FlatBuffer payload (identifier)
  * per-record CRC
* On corruption during recovery: stop at last valid record.

### 7.2 WAL record granularity

* WAL is **logical**, not physical.
* Records express operations like:

  * `Upsert(key, value_inline_or_vlog_ptr, metadata)`
  * `Tombstone(key, metadata)`
  * `TxnBegin(txn_id)`
  * `TxnCommit(txn_id)`
  * `TxnAbort(txn_id)` (optional)
  * `Checkpoint(lsn, ...)` (optional marker)

### 7.3 Redo-only with commit markers

* WAL may contain uncommitted operations.
* Recovery replays only operations belonging to transactions with a valid `TxnCommit`.
* No UNDO phase exists.

### 7.4 Group commit contract

* Default `group_commit_max_latency_ms = 5` (TOML configurable).
* COMMIT returns when txn is accepted into the durability pipeline.
* If crash happens before the fsync cycle includes the txn’s WAL records, the txn **may be lost**.

### 7.5 Write-ahead rule (strict)

* Dirty pages must not be flushed unless WAL is fsynced up to the page’s LSN.
* This is mandatory to keep corruption “nearly impossible” under power loss / partial writes.

### 7.6 Fuzzy checkpoints

* Background process:

  * establishes checkpoint LSN boundary
  * flushes eligible pages (subject to WAL fsync rule)
  * updates superblock last checkpoint LSN
  * makes older WAL segments eligible for deletion

### 7.7 Startup recovery procedure

1. Open directory; validate MANIFEST and format major.
2. Load superblocks; select newest valid.
3. Open WAL segments from last checkpoint onward.
4. Scan WAL records in order until corruption; stop at last valid.
5. Build set of committed txn IDs (from `TxnCommit` markers).
6. Replay logical ops for committed txns in LSN order, updating B+Tree pages and value references.
7. Truncate WAL to last valid record boundary (if needed).
8. Start background jobs (checkpoint, sweeper, value log GC).

---

## 8. Repair

### Trigger

* Auto-on-open if corruption detected (CRC failures, invalid superblock, WAL checksum failure, etc.)

### Allowed actions (conservative)

* Truncate WAL to last valid record.
* Drop unreferenced tail value-log segments.
* No full salvage rebuild.

### Outcome

* Repair runs, then DB opens read-write if consistent.

---

## 9. Cache and Memory

* Single global memory limit.
* Unified cache includes:

  * B+Tree pages
  * value log blocks / decoded value representations (implementation detail)
* Eviction is **LRU only** in v1.
* Cache statistics exposed through `INFO`.

---

## 10. Configuration, Introspection, and Admin

### Config

* TOML file, one DB per server.
* Includes:

  * `group_commit_max_latency_ms` (default 5)
  * cache memory limit
  * checkpoint interval knobs
  * sweeper interval
  * value log GC thresholds + periodic interval
  * listen address/port
  * log level

### Introspection

* `INFO` returns plain text, including:

  * format version, schema versions
  * WAL current LSN, last checkpoint LSN
  * cache size/usage/hit rate
  * txn stats (active, commits/sec, aborts/sec)
  * vlog stats (segments, live ratio, last GC)
  * TTL sweeper stats

### Admin CLI (`jubectl`)

* `info`
* `checkpoint`
* `repair`
* `validate` (walk pages, verify CRCs, basic invariants)
* `dump-manifest`

---

# Development Plan and Repo Standards (C++)

## 1) Repository structure

Suggested layout (matches the modular boundaries already defined):

* `schemas/` (`wire.fbs`, `wal.fbs`, `disk.fbs`)
* `src/wire/` (framing, FlatBuffers dispatch)
* `src/txn/` (overlay, planning, execution)
* `src/lock/` (lock manager)
* `src/storage/`

  * `pager/` (page IO, CRC, cache)
  * `btree/` (page formats + operations)
  * `wal/` (append, group commit, segment mgmt)
  * `vlog/` (segments, read/write, GC)
  * `checkpoint/`
  * `ttl/` (read-time checks + sweeper)
* `src/meta/` (manifest/superblock)
* `src/repair/`
* `src/config/` (TOML)
* `tools/jubectl/`
* `tests/` (unit tests)

## 2) Workflow

* Trunk-based development on `main`.
* Short-lived branches; PR required.

## 3) Commit guidelines (Conventional Commits)

Required format:

* `feat:`, `fix:`, `perf:`, `refactor:`, `docs:`, `test:`, `build:`, `ci:`, `chore:`
* Breaking change uses `!` + footer.

## 4) Required checks before merge (v1)

* Code format
* Lint
* Unit tests

---

# “Ready to start” checklist (first implementation milestones)

1. **Schemas**

* Write `wire.fbs`: TxnRequest/TxnResponse, basic ops, Value union, errors.
* Write `wal.fbs`: TxnBegin/Op/Commit + checksums/LSN fields.
* Write `disk.fbs`: manifest + superblock + page record structs/unions.

2. **Minimal storage**

* Pager + `data.pages` allocation/free list.
* B+Tree insert/lookup/delete with inline values only.

3. **WAL + recovery**

* WAL append + segmented files + per-record CRC.
* Recovery scan to last valid record.
* Commit markers + redo replay.

4. **Transactions**

* Key table parsing, lock acquisition ordering.
* Overlay read-your-writes.
* Apply ops → WAL → commit marker → page updates.

5. **Value log**

* Out-of-line storage for large values + pointer refs.
* GC scaffolding (can be no-op initially, but file format must be stable).

6. **TTL**

* Read-time expiration check using mono-based wall estimate.
* Sweeper produces tombstones.

7. **Server + CLI**

* Thread pool request handling.
* `INFO` + `jubectl validate/repair`.
