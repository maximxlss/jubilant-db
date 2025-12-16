#pragma once

#include "lock/lock_manager.h"
#include "meta/manifest.h"
#include "meta/superblock.h"
#include "server/transaction_receiver.h"
#include "server/worker.h"
#include "storage/btree/btree.h"
#include "storage/wal/wal_manager.h"
#include "txn/transaction_request.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace jubilant::server {

class Server {
public:
  Server(std::filesystem::path base_dir, std::size_t worker_count);
  ~Server();

  void Start();
  void Stop();

  bool SubmitTransaction(txn::TransactionRequest request);
  std::vector<TransactionResult> DrainCompleted();

  [[nodiscard]] bool running() const noexcept;

private:
  std::filesystem::path base_dir_;
  std::size_t worker_count_{0};
  std::atomic<bool> running_{false};

  lock::LockManager lock_manager_;
  storage::btree::BTree btree_;
  storage::wal::WalManager wal_manager_;
  meta::ManifestStore manifest_store_;
  meta::SuperBlockStore superblock_store_;

  TransactionReceiver receiver_{};
  std::shared_mutex btree_mutex_;

  std::vector<std::unique_ptr<Worker>> workers_;
  std::mutex results_mutex_;
  std::vector<TransactionResult> completed_transactions_;
}; 

} // namespace jubilant::server
