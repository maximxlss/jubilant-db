#include "server/worker.h"

#include <algorithm>
#include <ranges>
#include <utility>

namespace jubilant::server {

Worker::KeyLockGuard::KeyLockGuard(lock::LockManager& manager, std::string key, lock::LockMode mode)
    : manager_(manager), key_(std::move(key)), mode_(mode), owns_lock_(true) {
  manager_.Acquire(key_, mode_);
}

Worker::KeyLockGuard::~KeyLockGuard() {
  if (owns_lock_) {
    manager_.Release(key_, mode_);
  }
}

Worker::KeyLockGuard::KeyLockGuard(KeyLockGuard&& other) noexcept
    : manager_(other.manager_), key_(std::move(other.key_)), mode_(other.mode_),
      owns_lock_(other.owns_lock_) {
  other.owns_lock_ = false;
}

Worker::KeyLockGuard& Worker::KeyLockGuard::operator=(KeyLockGuard&& other) noexcept {
  if (this != &other) {
    if (owns_lock_) {
      manager_.Release(key_, mode_);
    }
    key_ = std::move(other.key_);
    mode_ = other.mode_;
    owns_lock_ = other.owns_lock_;
    other.owns_lock_ = false;
  }
  return *this;
}

Worker::Worker(std::string name, TransactionReceiver& receiver, lock::LockManager& lock_manager,
               storage::btree::BTree& btree, std::shared_mutex& btree_mutex,
               CompletionFn on_complete)
    : name_(std::move(name)), receiver_(receiver), lock_manager_(lock_manager), btree_(btree),
      btree_mutex_(btree_mutex), on_complete_(std::move(on_complete)) {}

Worker::~Worker() {
  Stop();
}

void Worker::Start() {
  if (running_.exchange(true)) {
    return;
  }

  thread_ = std::thread([this]() { Run(); });
}

void Worker::Stop() {
  if (!running_.exchange(false)) {
    return;
  }

  receiver_.Stop();

  if (thread_.joinable()) {
    thread_.join();
  }
}

bool Worker::running() const noexcept {
  return running_.load();
}

void Worker::Run() {
  while (running_.load()) {
    auto request = receiver_.Next();
    if (!request.has_value()) {
      if (receiver_.stopped()) {
        break;
      }
      continue;
    }

    auto result = Process(*request);
    if (on_complete_) {
      on_complete_(std::move(result));
    }
  }
}

TransactionResult Worker::Process(const txn::TransactionRequest& request) {
  TransactionResult result{};
  result.id = request.id;

  if (!request.Valid()) {
    result.state = txn::TransactionState::kAborted;
    return result;
  }

  [[maybe_unused]] const auto key_guards = AcquireTransactionLocks(request);
  txn::TransactionContext context{request.id};
  for (const auto& operation : request.operations) {
    const auto key = request.ResolveKey(operation);
    if (!key.has_value()) {
      result.state = txn::TransactionState::kAborted;
      context.MarkAborted();
      return result;
    }

    switch (operation.type) {
    case txn::OperationType::kGet:
      ApplyRead(operation, *key, context, result);
      break;
    case txn::OperationType::kSet:
      ApplyWrite(operation, *key, context, result);
      break;
    case txn::OperationType::kDelete:
      ApplyDelete(operation, *key, context, result);
      break;
    case txn::OperationType::kAssertExists:
    case txn::OperationType::kAssertNotExists:
    case txn::OperationType::kAssertType:
    case txn::OperationType::kAssertIntEq:
    case txn::OperationType::kAssertBytesHashEq:
    case txn::OperationType::kAssertStringHashEq:
    default:
      result.state = txn::TransactionState::kAborted;
      context.MarkAborted();
      return result;
    }

    if (context.state() == txn::TransactionState::kAborted) {
      result.state = context.state();
      return result;
    }
  }

  CommitTransaction(request, context);
  context.MarkCommitted();
  result.state = context.state();
  return result;
}

void Worker::ApplyRead(const txn::Operation& operation, std::string_view key,
                       txn::TransactionContext& context, TransactionResult& result) {
  OperationResult op_result{};
  op_result.type = operation.type;
  op_result.key = key;
  op_result.key_id = operation.key_id;

  const auto found = context.ReadThrough(std::string{key},
                                         [this, &key]() -> std::optional<storage::btree::Record> {
                                           std::shared_lock tree_guard{btree_mutex_};
                                           return btree_.Find(std::string{key});
                                         });
  if (found.has_value()) {
    op_result.success = true;
    op_result.value = found;
  }

  result.operations.push_back(std::move(op_result));
}

void Worker::ApplyWrite(const txn::Operation& operation, std::string_view key,
                        txn::TransactionContext& context, TransactionResult& result) {
  OperationResult op_result{};
  op_result.type = operation.type;
  op_result.key = key;
  op_result.key_id = operation.key_id;

  if (!operation.value.has_value()) {
    result.state = txn::TransactionState::kAborted;
    context.MarkAborted();
    result.operations.push_back(std::move(op_result));
    return;
  }

  context.Write(std::string{key}, *operation.value);
  op_result.success = true;
  op_result.value = operation.value;

  result.operations.push_back(std::move(op_result));
}

void Worker::ApplyDelete(const txn::Operation& operation, std::string_view key,
                         txn::TransactionContext& context, TransactionResult& result) {
  OperationResult op_result{};
  op_result.type = operation.type;
  op_result.key = key;
  op_result.key_id = operation.key_id;

  const auto existing = context.ReadThrough(
      std::string{key}, [this, &key]() -> std::optional<storage::btree::Record> {
        std::shared_lock tree_guard{btree_mutex_};
        return btree_.Find(std::string{key});
      });

  op_result.success = existing.has_value();
  context.StageDelete(std::string{key});

  result.operations.push_back(std::move(op_result));
}

std::vector<Worker::KeyLockGuard>
Worker::AcquireTransactionLocks(const txn::TransactionRequest& request) {
  std::vector<std::reference_wrapper<const txn::KeySpec>> sorted_keys(request.keys.begin(),
                                                                      request.keys.end());
  std::ranges::sort(sorted_keys, [](const std::reference_wrapper<const txn::KeySpec>& lhs,
                                    const std::reference_wrapper<const txn::KeySpec>& rhs) {
    return lhs.get().key < rhs.get().key;
  });

  std::vector<KeyLockGuard> key_guards;
  key_guards.reserve(sorted_keys.size());
  for (const auto& key_spec : sorted_keys) {
    key_guards.emplace_back(lock_manager_, key_spec.get().key, key_spec.get().mode);
  }
  return key_guards;
}

void Worker::CommitTransaction(const txn::TransactionRequest& request,
                               txn::TransactionContext& context) {
  std::unique_lock tree_guard{btree_mutex_};
  for (const auto& key_spec : request.keys) {
    if (!context.HasOverlayEntry(key_spec.key)) {
      continue;
    }

    const auto staged_value = context.Read(key_spec.key);
    if (staged_value.has_value()) {
      btree_.Insert(key_spec.key, *staged_value);
    } else {
      [[maybe_unused]] const bool erased = btree_.Erase(key_spec.key);
    }
  }
}

} // namespace jubilant::server
