# Code review findings (v0.0.2 focus)

This note captures issues surfaced during a full sweep of the v0.0.2 codebase so they remain visible
while the networked transaction milestone firms up.

## Reliability gaps

- **WAL never participates in request handling:** `server::Server` owns a `storage::wal::WalManager`
  but never routes operations through it, leaving writes to rely solely on B+Tree/pager persistence
  without any write-ahead logging or crash replay. Server paths and the new network adapter therefore
  do not meet the durability story outlined in the README (B+Tree + WAL + value log) or the v0.0.2
  plan until WAL hooks are introduced (e.g., `server::Worker` applying operations should append WAL
  records and advance LSNs) before acknowledging transactions. 【src/server/server.h】【src/server/server.cpp】
- **Network path still lacks end-to-end coverage:** Integration tests for remote `set/get/del` remain
  absent (ExecPlan step 8). The network server and Python/C++ clients are only exercised indirectly
  via unit tests and the manual stubs, so regressions in framing/dispatch would go unnoticed until
  manual testing. Bridging this gap should be prioritized to keep the v0.0.2 deliverables reliable.

## Stability fixes captured in this sweep

- **Lock manager shared-read test was scheduler-sensitive:** `LockManagerTest.AllowsConcurrentSharedAccess`
  occasionally under-counted concurrent shared lock holders (observed max_readers < thread count) when
  some threads released before the slowest thread acquired the shared mutex. Synchronizing readers at
  acquisition time makes the test deterministic and better reflects the intended shared lock behavior. 【tests/lock_manager_tests.cpp】

## Operational polish

- **`jubildb_server --help` now reports success:** The bootstrap previously returned a non-zero exit
  code even when only usage was requested, which could mislead wrappers/automation. ParseArgs now
  surfaces help requests separately so `--help` exits cleanly with status 0. 【src/server/main.cpp】
