## v0.0.1 Skeleton Plan (updated, ready to execute)

### Status snapshot

* **Implemented:** In-memory B+Tree insert/find/delete, pager page IO and allocation skeleton, manifest/superblock stubs, and the `SimpleStore` wrapper that powers `jubectl init/set/get/del` with UTF-8 key validation and overwrite semantics.
* **Tested:** Persistence and CRUD semantics for `SimpleStore`, B+Tree overwrite/delete behavior, pager read/write parity, manifest/superblock round-trips, WAL shell, lock manager basics, and transaction context overlays (`tests/*.cpp`). Run `cmake --preset dev-debug` followed by `ctest --preset dev-debug` to exercise coverage.
* **Not yet covered:** WAL durability guarantees, crash recovery, concurrency, TTL enforcement, and value-log plumbing.

### Target outcome

A tiny end-to-end local DB you can run via `jubectl`:

* `init` creates a DB directory
* `set/get/del` work
* clean shutdown + reopen preserves data

No WAL, no transactions, no server, no TTL, no value log.

---

### How to use this checklist

* Treat each numbered section as a milestone with a testable definition of done.
* Prefer writing the unit/integration test immediately after sketching the API so TDD can drive the implementation.
* Capture TODOs in headers and brief docstrings instead of leaving implicit assumptions.

---

## 0) Repo + build (Day 0)

**Deliverables**

* CMake builds:

* `jubectl`
  * `unit_tests`
* FlatBuffers build step (generate C++ from `disk.fbs` and optionally `wire.fbs`)
* clang-format, clang-tidy configs in repo
* Conventional Commits enforced socially (CI later)

**Key decisions**

* Page size constant for v0.0.1: **4096 bytes**

---

## 1) Minimal on-disk layout (Day 1)

### DB directory contents

* `MANIFEST` (size-prefixed FlatBuffer)
* `SUPERBLOCK_A` (binary struct or FlatBuffer; include CRC + generation)
* `SUPERBLOCK_B` (same; initially optional to write both, but file must exist)
* `data.pages` (fixed-size page file)

### Manifest fields (minimum)

* `format_major`, `format_minor`
* `db_uuid` (random)
* `page_size = 4096`
* `inline_threshold` (store a value, but v0.0.1 always inlines)
* schema version placeholders (strings or ints)

### Superblock fields (minimum)

* `generation` (u64)
* `root_page_id` (u64)
* `crc` (u32/u64)
* (future fields can be reserved padding)

**Open logic**

* Read both superblocks; pick the one with valid CRC and highest generation.
* If none valid: treat as new DB.

**Close logic**

* Ensure superblock is updated to point to current root.
* Fsync metadata + `data.pages`.

---

## 2) Pager (Day 1–2)

### Pager responsibilities

* Open/create `data.pages`
* Read/write pages at fixed offsets: `offset = page_id * 4096`
* Allocate new pages (simple monotonic allocator is fine for v0.0.1)

### Minimal API

* `Pager::open(path)`
* `Page Pager::read(page_id)`
* `void Pager::write(page_id, Page)`
* `page_id Pager::alloc_page(PageType)`
* `void Pager::fsync()`

### Page header (inside each page)

* `page_id` (u64)
* `page_type` (u16)
* `crc` (u32)
* (rest is payload)

CRC can be computed and stored; for v0.0.1 you can enforce it only in tests (but implementing it now reduces churn).

**Deliverable**

* Unit test: allocate → write known bytes → read back exact.

---

## 3) Minimal B+Tree (Day 2–4)

### Simplifications allowed in v0.0.1

* Only implement splitting on insert.
* Deletes can be “remove entry from leaf” without merge/rebalance.
* No range scan required.
* Single-threaded.

### Key/value encoding in leaf

* Key: `u32 key_len + key_bytes` (must be valid UTF-8 on input)
* Value: tagged union:

  * `Bytes`: `u32 len + bytes`
  * `String`: `u32 len + utf8 bytes` (validate)
  * `Int64`: `i64`

(You can use FlatBuffers `Value` union right away if you prefer; for v0.0.1 the simple encoding is fastest to implement and easy to swap later.)

### Operations

* `get(key) -> optional<Value>`
* `set(key, Value) -> void` (overwrite if exists)
* `del(key) -> bool` (true if existed)

**Deliverables**

* Insert 10k keys, verify reads.
* Overwrite behavior test.
* Delete behavior test.
* Persistence test: create → set N → close → reopen → verify.

---

## 4) `jubectl` CLI (Day 3–4)

### Commands

* `jubectl init <db_dir>`
* `jubectl set <db_dir> <key> bytes <hex>`
* `jubectl set <db_dir> <key> string <utf8>`
* `jubectl set <db_dir> <key> int <int64>`
* `jubectl get <db_dir> <key>`
* `jubectl del <db_dir> <key>`

Optional debug:

* `jubectl stats <db_dir>` (page count, root id)

**Bytes input**

* hex string (even length), decode to raw bytes.

---

## 5) v0.0.1 correctness guarantees (explicit in README)

Guaranteed:

* Correct GET/SET/DEL semantics
* UTF-8 key validation
* Persistence across clean shutdown

Not guaranteed (yet):

* Crash safety (no WAL)
* Concurrency/thread safety
* Transactions, TTL, value log, repair

---

## 6) Definition of Done

* `jubectl init/set/get/del` works end-to-end
* Persistence test passes
* Unit tests in CI pass (format/lint can be added later, but at least build+tests)

---

### Suggested first commit sequence (Conventional Commits)

1. `chore: scaffold repo with cmake and tool targets`
2. `feat(meta): add manifest and superblock read/write`
3. `feat(pager): implement fixed-size page io and allocator`
4. `feat(btree): implement leaf insert/find and root handling`
5. `feat(cli): add jubectl init/set/get/del`
6. `test: add persistence and overwrite/delete unit tests`
