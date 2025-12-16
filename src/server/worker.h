#pragma once

#include "lock/lock_manager.h"
#include "server/transaction_receiver.h"
#include "storage/btree/btree.h"
#include "txn/transaction_context.h"
#include "txn/transaction_request.h"

#include <atomic>
#include <functional>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

namespace jubilant::server {

struct OperationResult {
  txn::OperationType type{txn::OperationType::kGet};
  std::string key;
  bool success{false};
  std::optional<storage::btree::Record> value;
};

struct TransactionResult {
  std::uint64_t id{0};
  txn::TransactionState state{txn::TransactionState::kPending};
  std::vector<OperationResult> operations;
};

class Worker {
public:
  using CompletionFn = std::function<void(TransactionResult)>;

  Worker(std::string name, TransactionReceiver& receiver, lock::LockManager& lock_manager,
         storage::btree::BTree& btree, std::shared_mutex& btree_mutex, CompletionFn on_complete);
  ~Worker();

  void Start();
  void Stop();

  [[nodiscard]] bool running() const noexcept;

private:
  class KeyLockGuard {
  public:
    KeyLockGuard(lock::LockManager& manager, std::string key, lock::LockMode mode);
    ~KeyLockGuard();

    KeyLockGuard(const KeyLockGuard&) = delete;
    KeyLockGuard& operator=(const KeyLockGuard&) = delete;

    KeyLockGuard(KeyLockGuard&&) = delete;
    KeyLockGuard& operator=(KeyLockGuard&&) = delete;

  private:
    lock::LockManager& manager_;
    std::string key_;
    lock::LockMode mode_;
  };

  void Run();
  TransactionResult Process(const txn::TransactionRequest& request);
  void ApplyRead(const txn::Operation& operation, txn::TransactionContext& context,
                 TransactionResult& result);
  void ApplyWrite(const txn::Operation& operation, txn::TransactionContext& context,
                  TransactionResult& result);
  void ApplyDelete(const txn::Operation& operation, TransactionResult& result);

  std::string name_;
  TransactionReceiver& receiver_;
  lock::LockManager& lock_manager_;
  storage::btree::BTree& btree_;
  std::shared_mutex& btree_mutex_;
  CompletionFn on_complete_;

  std::atomic<bool> running_{false};
  std::thread thread_;
};

} // namespace jubilant::server
