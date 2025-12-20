# ExecPlan: Integrate conditional multi-operation transactions with concurrency validation

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and `Outcomes & Retrospective` must be kept up to date as work proceeds. Maintain this file in accordance with `PLANS.md` at the repository root and treat it as the single source of truth for implementing conditional transactions with concurrency guarantees.

## Purpose / Big Picture

Enable users to run multi-key transactions that mix reads, conditional assertions, and writes in a single request while preserving strict serializability under load. The end result should let a client issue a transaction containing predeclared read/write sets and ASSERT-style conditionals (for example, assert a key exists before overwriting) and observe that concurrent workers execute them quickly without violating isolation. The capability will be demonstrated through targeted concurrency tests that fail today and pass after the implementation.

## Progress

- [x] (2025-12-18 10:52:38Z) Drafted initial ExecPlan based on the transaction pipeline, lock manager, and specification.
- [x] (2025-12-18 11:31:18Z) Added work-parallelization tracks and shared contracts so multiple contributors can advance independently.
- [x] (2025-12-18 13:10:12Z) Prepared execution scaffolding: relocated the plan under `docs/execplans/`, enumerated shared headers/builders, and clarified sequencing so tracks can start immediately.
- [x] (2025-12-18 13:30:26Z) Published header-first scaffolding in code: added key tables and derived lock modes to transaction requests, plumbed key IDs through workers and network parsing, introduced overlay-first context helpers, and seeded validation/unit tests so concurrent tracks can build against stable shapes.
- [x] (2025-12-18 19:55:15Z) Hardened transaction requests with lock-mode-aware validation and expectation gating so declared key tables drive canonical locking and parsing errors surface early.
- [x] (2025-12-18 19:55:15Z) Reworked worker execution to acquire declared locks in sorted order, stage overlay mutations, and commit once after successful validation; aborted transactions now leave storage untouched for future concurrency tests.
- [ ] (2025-12-18 10:52:38Z) Add concurrency-focused unit and integration tests that prove correctness (no isolation violations) and exercise overlapping transactions for performance characteristics.
- [ ] (2025-12-18 10:52:38Z) Document the new transaction semantics in README/docs and adjust examples for conditional operations.
- [ ] (2025-12-18 10:52:38Z) Finalize retrospective after tests pass and concurrency guarantees are demonstrated.

## Surprises & Discoveries

- Observation: ASSERT_* operations remain parsed-only; worker execution still aborts when encountering them. Evidence: `Worker::Process` falls through to the default case for ASSERT operations in `src/server/worker.cpp`.
- Observation: Staging writes and deletes in the transaction overlay while holding canonical locks prevents partial commits on aborts, which tightened the invariants for upcoming concurrency tests. Evidence: `Worker::CommitTransaction` flushes overlay entries only after the operation loop completes in `src/server/worker.cpp`.

## Decision Log

- Decision: Implement conditional operations using the ASSERT primitives specified in `MAIN_SPECIFICATION.md` §2.5 (ASSERT_EXISTS, ASSERT_NOT_EXISTS, ASSERT_TYPE, ASSERT_INT_EQ, ASSERT_BYTES_HASH_EQ, ASSERT_STRING_HASH_EQ) to avoid inventing new semantics and keep wire/runtime aligned with the published contract. Rationale: Aligns with the spec and keeps future FlatBuffers schema migration straightforward. Date/Author: 2025-12-18 / assistant.
- Decision: Keep shared contracts header-first. Publish header declarations and builder helpers before refactoring worker code so parallel tracks compile against stable types even if implementations are stubbed. Rationale: Minimizes blocking between request, context, worker, and test tracks. Date/Author: 2025-12-18 / assistant.

## Outcomes & Retrospective

Pending implementation; this section will summarize concurrency proof points, gaps, and follow-ups after the test suite lands.

## Context and Orientation

Transactions now include a declared key table: `txn::TransactionRequest` exposes `KeySpec` entries (with derived `lock::LockMode`s) plus operations keyed by `key_id`, and the builder in `src/txn/transaction_request.*` canonicalizes keys while widening locks for writes. ASSERT operations are represented in the enum but currently abort in `server::Worker` until evaluation lands. `TransactionContext` still hosts overlay state in `src/txn/transaction_context.*` with new helper methods for overlay-first reads and delete staging. The server pipeline uses `server::TransactionReceiver` as a bounded queue and `server::Worker` to process requests in threads (`src/server/worker.*`), acquiring the declared key set in canonical order via `lock::LockManager` (`src/lock/lock_manager.*`), staging mutations in the overlay, and committing to the BTree (`src/storage/btree/btree.*`) only after the operation loop succeeds. Tests cover single-worker processing, request builders, and overlay caching in `tests/server_worker_tests.cpp`, `tests/transaction_request_tests.cpp`, and `tests/transaction_context_tests.cpp`. ASSERT evaluation and concurrency proof tests still need to land.

## Plan of Work

Begin by stabilizing shared headers and builders so every track can compile immediately: publish key-table structs, expanded operation enums, and lightweight transaction builders in `src/txn/transaction_request.h` (with no-op implementations if needed) plus overlay/ASSERT helper signatures in `src/txn/transaction_context.h`. Once those contracts exist, extend the transaction request model to include key tables with per-key modes (read or read-write) and an expanded operation enum covering ASSERT variants. Update validation to reject missing declarations, mismatched modes, or unsupported conditionals.

Next, refactor the worker execution path to construct a transaction view: consult the overlay before storage reads, apply conditionals against the overlay-plus-storage view, and batch lock acquisition by sorting declared keys to enforce canonical ordering before executing operations. Ensure writes stage in the overlay and commit to the BTree only after all conditionals pass, integrating with existing lock manager semantics and preparing hooks for WAL in future steps. Enhance the client/server framing (JSON transaction shape for now) to accept the richer request structure, keeping compatibility with the preview wire doc where possible.

Introduce unit tests that exercise conditional logic locally (for example, ASSERT_NOT_EXISTS followed by SET, ASSERT_INT_EQ guarding an increment) and integration-style tests that spin up multiple workers sharing a `TransactionReceiver` to submit overlapping transactions. Design concurrency tests to include a mix of conflicting and non-conflicting key sets to show parallel execution (non-overlapping keys complete concurrently) while conflicting transactions serialize according to lock ordering without deadlocks or lost updates. Add timing/throughput assertions where practical (for example, measuring that non-conflicting transactions complete within a bounded window compared to sequential execution). Update docs and examples to illustrate how to craft conditional transactions and interpret results.

## Work Parallelization

Multiple contributors can proceed with minimal coordination by aligning on the interfaces defined in the `Interfaces and Dependencies` section. The following tracks rely on the shared headers and builder helpers; publish those early so parallel work can start before implementations are finished:

- Request and validation track: own the `src/txn/transaction_request.*` changes that add `KeySpec`, the expanded `OperationType` enum, validation helpers, and JSON parsing/builders. Deliver sample builders used by tests so other tracks can construct transactions without new wiring work. Provide a header-only `BuildTransactionRequest` helper usable before runtime logic lands.
- Transaction context track: extend `src/txn/transaction_context.*` to add overlay-first reads, cached lookups, ASSERT evaluation helpers, and commit staging. Expose `PrepareExecutionPlan`/`ApplyOperation` signatures up front so workers can call them even if the internal logic is stubbed during parallel development.
- Worker execution track: refactor `src/server/worker.*` to adopt the new request shapes, acquire locks using sorted `KeySpec` entries, and route all operations through the enhanced `TransactionContext`. Once the context API stabilizes, this track can proceed without waiting on docs or tests by temporarily mocking ASSERT evaluation until the transaction-context track finishes.
- Concurrency test and harness track: add builders and timing assertions in `tests/transaction_context_tests.cpp` and `tests/server_worker_tests.cpp` (or a new integration file). This track can start once the request builders exist, using temporary fakes for worker/context behavior until the concrete implementations land.
- Documentation and wire track: update `docs/txn-wire-v0.0.2.md`, `README.md`, and example payloads to reflect key tables and ASSERT operations. This work can proceed in parallel so long as it stays consistent with the request schema produced by the request track.

Each track should publish any new structs or helper signatures in headers early (even with stub implementations) so others can compile against them without synchronous coordination. Keep integration points narrow: only the agreed request structs, context APIs, and worker entry points should be required across tracks.

## Concrete Steps

The numbered steps align with the tracks above; steps 0–5 can progress concurrently once the shared headers are in place, and step 6 should run after code lands.

0. Publish shared scaffolding:
   - Add key-table structs, ASSERT operation enum values, and transaction builder helpers in `src/txn/transaction_request.h` with minimal no-op implementations in `transaction_request.cpp` so code compiles immediately.
   - Add overlay-first read helpers, ASSERT evaluation entry points, and commit-staging hooks in `src/txn/transaction_context.h` with stubbed bodies in `transaction_context.cpp` that return explicit TODO status codes for now.
   - Document the stubbed behavior and TODOs directly in the headers so downstream tracks understand expectations and can integrate without guessing.
   - Status (2025-12-18 13:30:26Z): Completed. Builders now derive key tables and lock modes; worker/network parsing honor key IDs; overlay helpers exist for read-through and delete staging; ASSERT operations abort early until evaluation lands; new request/context unit tests keep scaffolding stable for parallel tracks.
1. Expand transaction definitions in `src/txn/transaction_request.h/.cpp` to add key-table structures, ASSERT operation variants, and stricter validation (unknown key IDs, mode mismatches, missing values). Reflect these changes in any request builders or parsers used by the server.
   - Status (2025-12-18 19:55:15Z): Completed. Builders derive canonical lock modes, validation rejects weaker declared locks or mismatched expectations, and network parsing continues to surface malformed ASSERT payloads early.
2. Augment `src/txn/transaction_context.*` to maintain both overlay writes and cached reads, exposing helpers for ASSERT evaluation and a way to stage commit-ready mutations separate from runtime state transitions.
3. Refactor `src/server/worker.*` to:
   - Pre-collect declared keys from the request, sort them, and acquire shared/exclusive locks before executing operations.
   - Evaluate ASSERT operations against the overlay-first view, aborting the transaction without partial commits on failure.
   - Apply GET/SET/DELETE using the overlay to provide read-your-writes and defer durable BTree updates until after all checks succeed.
   - Status (2025-12-18 19:55:15Z): Partially completed. Workers now acquire the full declared key set in sorted order, stage overlay mutations, and commit once at the end; ASSERT evaluation remains stubbed and will be wired into the overlay view next.
4. Add concurrency-aware tests:
   - Unit tests for ASSERT evaluation and overlay behavior in `tests/transaction_context_tests.cpp` or a new `tests/transaction_planner_tests.cpp`.
   - Multi-worker tests in `tests/server_worker_tests.cpp` (or a dedicated integration test) that enqueue interleaved transactions with overlapping keys to verify serialization and with disjoint keys to verify parallelism and performance.
5. Update docs: extend `docs/txn-wire-v0.0.2.md` and `README.md` examples to describe conditional operations and the single-frame transaction structure with key tables and declared modes.
6. Run tooling: cmake --preset dev-debug, cmake --build --preset dev-debug --target clang-format, cmake --preset dev-debug-tidy, cmake --build --preset dev-debug-tidy, then ctest --preset dev-debug (and focused -R filters for new tests) to confirm correctness and lints.

## Validation and Acceptance

Acceptance requires reproducible evidence that conditional transactions enforce correctness and remain fast under concurrency:

- New unit tests covering ASSERT_* behaviors and overlay read-your-writes fail before the implementation and pass afterward.
- Multi-worker tests demonstrate serializable outcomes: conflicting transactions serialize without deadlocks or lost updates, and disjoint key sets complete faster than a forced-sequential baseline (for example, measured via timing assertions or completion counts within a time budget).
- End-to-end transaction examples in docs show how to send a transaction mixing ASSERT and SET/GET/DELETE, and the server responds with per-operation success flags matching the described semantics.

## Idempotence and Recovery

Code and tests should be safe to re-run: transaction planners should handle repeated key declarations deterministically, lock acquisition must use sorted keys to avoid deadlocks on retries, and tests should clean up temporary directories (`std::filesystem::remove_all`) before recreating storage structures. If a test allocates threads or receivers, ensure they are stopped/joined even on assertion failures by using RAII guards or ASSERT_NO_FATAL_FAILURE patterns.

## Artifacts and Notes

Capture key evidence such as:

- Snippets of transaction request JSON demonstrating key tables and ASSERT operations.
- Test output showing concurrent transaction completions and assertions about ordering or timing.
- Any decision changes captured in the `Decision Log` with rationale and date.

## Interfaces and Dependencies

Define the following interfaces to keep the implementation stable and unblock parallel work. Declarations should land early even if bodies are temporary:

- Transaction request model additions in `src/txn/transaction_request.h`:
    struct KeySpec { uint32_t id; LockMode mode; std::string key; };
    enum class OperationType { kGet, kSet, kDelete, kAssertExists, kAssertNotExists, kAssertType, kAssertIntEq, kAssertBytesHashEq, kAssertStringHashEq };
    struct Operation { OperationType type; uint32_t key_id; std::optional<storage::btree::Record> value; std::optional<ExpectedTypeOrHash> expected; };
    TransactionRequest BuildTransactionRequest(std::vector<KeySpec> keys, std::vector<Operation> ops); // builder to unblock tests and worker wiring.
- Transaction context expectations in `src/txn/transaction_context.h`:
    class TransactionContext { Result<ReadResult> Read(uint32_t key_id); Result<void> StageWrite(uint32_t key_id, const storage::btree::Record& value); Result<AssertResult> EvaluateAssert(const Operation& op); Result<void> CommitStaged(); };
    Provide overlays for read-your-writes, cached lookups to avoid repeated BTree access, and explicit statuses for ASSERT failures.
- Worker executor expectations in `src/server/worker.*`:
    Use sorted `KeySpec` entries to acquire locks before executing operations; expose a helper to evaluate ASSERT operations against a `TransactionContext` that merges overlay and BTree reads; ensure failures set transaction state to aborted and release locks cleanly.
- Testing utilities:
    Helper functions to build transaction requests with key tables and operation lists, plus timing harnesses to submit batches through `TransactionReceiver` to multiple `Worker` instances for concurrency validation.

Revision Note (2025-12-18 11:31:18Z): Added explicit work-parallelization tracks and clarified how contributors can progress concurrently with shared headers and narrow integration points.
Revision Note (2025-12-18 13:10:12Z): Relocated the plan to `docs/execplans/`, added header-first scaffolding tasks, and detailed shared interfaces and builder helpers so parallel implementation work can start immediately.
