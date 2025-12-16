## Appendix A — Recommended Technology Stack for Development

This appendix defines a **concrete, realistic technology stack** suitable for implementing *Jubilant DB v1* exactly as specified. The stack emphasizes **correctness, performance, debuggability, and long-term maintainability**, without over-engineering or introducing unnecessary runtime dependencies.

The choices below are **recommendations**, not requirements, but they are internally consistent and aligned with the spec’s constraints.

---

## A.1 Programming Language & Standard

### Language

**C++20**

**Rationale**

* Required for:

  * fine-grained control over memory, IO, and layout
  * interoperability with C ABI / zero-copy expectations
  * predictable performance for B+Tree, WAL, and caches
* C++20 gives:

  * `std::span`, `std::byte`
  * coroutines (optional later)
  * `std::atomic_ref` (useful for lock-free counters)
  * improved constexpr for schema/version checks

### Compiler targets

* **Clang ≥ 16** (primary)
* **GCC ≥ 13** (secondary)
* MSVC optional, not required for v1

Compile flags (recommended baseline):

```
-std=c++20 -Wall -Wextra -Werror
-fno-exceptions (optional but recommended)
-fno-rtti (optional, if architecture allows)
```

---

## A.2 Build System

### Build tool

**CMake ≥ 3.25**

**Rationale**

* Ubiquitous, CI-friendly, IDE-agnostic
* Excellent FlatBuffers integration
* Supports multi-target builds (server, CLI, tests)

Recommended layout:

```
/CMakeLists.txt
/cmake/Toolchains.cmake
/src/
/schemas/
/tools/
/tests/
```

### Dependency management

* **FetchContent** for small, version-pinned deps
* No global/system dependency assumptions
* Third-party libraries pinned in CMake:
  * **FlatBuffers v24.3.25** (schemas + codegen)
  * **GoogleTest v1.14.0** (unit tests)
  * **toml++ v3.4.0** (TOML configuration loader)

---

## A.3 Serialization & Schema

### FlatBuffers

**FlatBuffers ≥ 24.x**

Used for:

* wire protocol (`wire.fbs`)
* WAL records (`wal.fbs`)
* on-disk structures (`disk.fbs`)

**Guidelines**

* Use **file identifiers** on every FlatBuffer type.
* Size-prefix all FlatBuffer blobs (`GetSizePrefixedRoot`).
* Never rely on implicit defaults for disk formats.
* Generate C++ code with:

  ```
  flatc --cpp --scoped-enums --gen-object-api
  ```

---

## A.4 Networking

### Transport

* POSIX TCP sockets
* Blocking mode

### Abstraction

* Thin internal wrapper over:

  * `socket`, `bind`, `listen`, `accept`
  * `read`, `write`
* No external networking framework in v1

**Rationale**

* Predictable behavior
* Easy debugging
* No hidden async state

---

## A.5 Threading & Concurrency

### Thread pool

* Custom thread pool using:

  * `std::thread`
  * `std::mutex`
  * `std::condition_variable`
* Fixed-size pool configured at startup

### Locks

* Per-key lock table:

  * RW locks implemented with `std::shared_mutex` or custom RW lock
* Global canonical ordering enforced in txn planner

### Atomics

* `std::atomic<uint64_t>` for counters, LSNs, stats

---

## A.6 File IO & Durability

### File IO

* POSIX file APIs:

  * `open`, `pread`, `pwrite`, `fsync`, `fdatasync`
* Explicit offsets only (no implicit seek state)

### WAL fsync

* Single WAL writer thread (or serialized WAL append path)
* Group commit using:

  * batching
  * timer + size threshold (future)

### Page IO

* Fixed-size aligned buffers
* Page cache mediates all page reads/writes

---

## A.7 Checksums & Hashing

### Page checksums

* **CRC32C** or **CRC64**

  * CRC32C preferred if hardware acceleration available
* Library:

  * bundled reference implementation
  * or compiler intrinsic if available

### ASSERT hash ops

* Stable cryptographic hash:

  * **SHA-256** or **BLAKE3**
* Hash algorithm identifier stored in MANIFEST

---

## A.8 Configuration

### Format

**TOML**

### Library

* **toml++** (header-only)

**Rationale**

* Strong typing
* Human-readable
* Easy validation

---

## A.9 Logging

### Logging style

* Plain text logs

### Library

* **spdlog** (configured for synchronous logging)

Log levels:

* ERROR
* WARN
* INFO
* DEBUG
* TRACE (optional)

---

## A.10 CLI Tooling (`jubectl`)

### CLI framework

* **CLI11** or equivalent lightweight header-only library

Commands:

* `info`
* `checkpoint`
* `repair`
* `validate`
* `dump-manifest`

---

## A.11 Testing Stack

### Unit testing

* **Catch2** or **GoogleTest**
* Required for:

  * B+Tree operations
  * WAL encoding/decoding
  * FlatBuffers round-trips
  * Lock ordering logic

### Test utilities

* Temporary directories per test
* Fault injection helpers:

  * simulated torn WAL records
  * truncated files

---

## A.12 Tooling & Code Quality

### Formatting

* **clang-format**
* Enforced via CI and pre-commit hook

### Linting

* **clang-tidy**
* Focus on:

  * lifetime issues
  * undefined behavior
  * concurrency hazards

### Static analysis (optional but recommended)

* clang static analyzer
* AddressSanitizer / UndefinedBehaviorSanitizer builds

---

## A.13 Continuous Integration

### CI platform

**GitHub Actions**

### Required jobs

* Build (Debug + Release)
* Unit tests
* clang-format check
* clang-tidy check

---

## A.14 Repository Conventions

### Branching

* Trunk-based (`main`)
* Short-lived feature branches

### Commit messages

**Conventional Commits**, enforced by CI:

```
feat(storage): add page CRC validation
fix(wal): stop replay at last valid record
perf(cache): reduce page lookup overhead
```

---

## A.15 Versioning & Compatibility

### Versioning scheme

* **Semantic Versioning**
* Disk format version:

  * stored in MANIFEST
  * major mismatch → refuse open

---

## A.16 Minimal Initial Dependency List

| Purpose       | Library             |
| ------------- | ------------------- |
| Serialization | FlatBuffers         |
| Config        | toml++              |
| Logging       | spdlog              |
| CLI           | CLI11               |
| Testing       | Catch2 / GoogleTest |

All dependencies:

* pinned to exact versions
* vendored or FetchContent’d
* no system-global assumptions

---

## A.17 Explicit Non-Dependencies (v1)

Intentionally **not used**:

* Boost
* gRPC
* HTTP frameworks
* Async runtimes
* External databases
* OS-specific kernel features (io_uring, AIO)
