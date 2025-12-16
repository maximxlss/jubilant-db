# Server runtime scaffolding

The server runtime now includes basic plumbing for receiving and executing transaction requests. The goal is to keep lifecycle
and concurrency mechanics testable while the wire protocol and full transaction planner are still under construction.

## Components

* **TransactionRequest/Operation (src/txn/transaction_request.h):** A lightweight description of requested key-level actions.
  Validation ensures operations include keys and that writes carry a value.
* **TransactionReceiver (src/server/transaction_receiver.h):** A bounded, thread-safe queue that accepts validated
  `TransactionRequest` objects and blocks workers until new work arrives or shutdown is signaled.
* **Worker (src/server/worker.h):** A dedicated thread that pops requests from the receiver, acquires per-key locks, and
  applies reads/writes/deletes against the shared `BTree`. Results are surfaced through a completion callback.
* **Server (src/server/server.h):** Owns the shared storage state, worker pool, and receiver. New requests flow through
  `SubmitTransaction`, and callers can poll processed results via `DrainCompleted()`.

## Concurrency expectations

* Per-key locks are enforced through `LockManager`; BTree mutations additionally take a shared mutex to serialize map access.
* `TransactionReceiver::Stop()` wakes any waiting worker threads so shutdowns do not block.
* Workers mark transactions committed or aborted using `TransactionContext`, giving the upcoming planner/wire layers a stable
  place to plug in richer state transitions.

## Next steps

* Wire `TransactionReceiver` into the FlatBuffers-based wire layer once it lands.
* Extend `TransactionResult` to propagate structured responses back to clients.
* Replace the in-memory BTree implementation with the disk-backed pager/B+Tree stack while preserving the worker flow.
