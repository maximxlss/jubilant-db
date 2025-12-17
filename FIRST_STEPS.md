# Milestone v0.0.1 — CLI-first store

This milestone proves the storage engine by running `jubectl` end to end on a single node. It is intentionally small, but it must feel production-worthy: persistence across restarts, UTF-8 validation, overwrite semantics, and basic durability guardrails.

## Scope and outcomes

### What v0.0.1 delivers

- `jubectl init/set/get/del` backed by `SimpleStore` and the pager/B+Tree stack.
- UTF-8 key validation and overwrite semantics.
- MANIFEST generations persisted to disk with monotonic selection on load.
- Dual superblocks with CRC selection and generation tracking.
- WAL replay at startup to restore state after clean shutdowns.
- Observability via `jubectl stats` and `jubectl validate` (manifest + superblock metadata and corruption checks).

### What is explicitly out of scope

- Crash recovery beyond WAL replay (e.g., mid-flight crash hardening).
- Concurrency or TTL enforcement.
- Value-log plumbing and compaction.
- Wire protocol and server-facing transaction executor.

## Definition of done

- **CLI behavior:** `jubectl init/set/get/del/stats/validate` operate on a DB directory and print clear errors for invalid input.
- **Persistence:** Data written through `jubectl` survives clean restart; MANIFEST and superblocks select the newest valid generation.
- **WAL:** Recorded operations are replayed on startup; corruption is surfaced via CRC checks.
- **Tests:** `ctest --preset dev-debug` passes; unit coverage includes B+Tree insert/overwrite/delete, pager IO, manifest/superblock rotation, WAL replay, and `SimpleStore` persistence.
- **Build + lint:** CMake presets configure cleanly (`cmake --preset dev-debug`), and formatting passes (`--target clang-format`).

## Milestone checklist

1. **Repository + build wiring**
   - CMake builds `jubectl`, `unit_tests`, FlatBuffers codegen, and formatting targets.
   - Conventional Commits in use; clang-format/clang-tidy configs checked in.

2. **On-disk layout**
   - Files: `MANIFEST` (size-prefixed FlatBuffer), `SUPERBLOCK_A/B` (CRC + generation), `data.pages` (fixed-size page file).
   - Selection rules: choose the valid superblock with the highest generation, otherwise treat as new DB.

3. **Pager**
   - Fixed-size page IO at `page_id * page_size` offsets.
   - Monotonic page allocator; CRC/type tags kept in headers.
   - API: open/read/write/alloc/fsync.

4. **B+Tree (minimal)**
   - Insert with splitting, overwrite in place, delete by tombstoning a leaf entry (no merge/rebalance yet).
   - Key encoding: `u32 len + UTF-8 bytes`; value encoding supports bytes/string/int64.
   - Operations: `get`, `set`, `del` with bytewise lexicographic ordering.

5. **Store + CLI integration**
   - `SimpleStore` wraps pager + tree, exposes CRUD to `jubectl` and wires UTF-8 validation.
   - `jubectl stats` prints manifest generation/version, page sizing, active superblock root, checkpoint LSN, and page/key counts.
   - `jubectl validate` reruns manifest validation and superblock CRC selection.

6. **Durability and observability**
   - MANIFEST writes fsynced; superblocks updated on checkpoint/close.
   - WAL segments size-prefixed with CRC; replay on startup is deterministic.
   - Logging and error messages are actionable for failed validation.

## Developer loop

Run the standard presets while iterating:

```sh
cmake --preset dev-debug
cmake --build --preset dev-debug
ctest --preset dev-debug
```

Targeted formatting and linting are available through CMake build targets (e.g., `--target clang-format`) and the tidy-friendly preset `dev-debug-tidy`.
