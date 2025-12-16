#include "server/worker.h"

#include <utility>

namespace jubilant::server {

Worker::KeyLockGuard::KeyLockGuard(lock::LockManager& manager, std::string key, lock::LockMode mode)
    : manager_(manager), key_(std::move(key)), mode_(mode) {
  manager_.Acquire(key_, mode_);
}

Worker::KeyLockGuard::~KeyLockGuard() {
  manager_.Release(key_, mode_);
}

Worker::Worker(std::string name, TransactionReceiver& receiver, lock::LockManager& lock_manager,
               storage::btree::BTree& btree, std::shared_mutex& btree_mutex, CompletionFn on_complete)
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

  txn::TransactionContext context{request.id};
  for (const auto& operation : request.operations) {
    switch (operation.type) {
    case txn::OperationType::kGet:
      ApplyRead(operation, context, result);
      break;
    case txn::OperationType::kSet:
      ApplyWrite(operation, context, result);
      break;
    case txn::OperationType::kDelete:
      ApplyDelete(operation, result);
      break;
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

  context.MarkCommitted();
  result.state = context.state();
  return result;
}

void Worker::ApplyRead(const txn::Operation& operation, txn::TransactionContext& context,
                       TransactionResult& result) {
  OperationResult op_result{};
  op_result.type = operation.type;
  op_result.key = operation.key;

  KeyLockGuard key_guard{lock_manager_, operation.key, lock::LockMode::kShared};
  std::shared_lock tree_guard{btree_mutex_};
  const auto found = btree_.Find(operation.key);
  if (found.has_value()) {
    op_result.success = true;
    op_result.value = found;
    context.Write(operation.key, *found);
  }

  result.operations.push_back(std::move(op_result));
}

void Worker::ApplyWrite(const txn::Operation& operation, txn::TransactionContext& context,
                        TransactionResult& result) {
  OperationResult op_result{};
  op_result.type = operation.type;
  op_result.key = operation.key;

  if (!operation.value.has_value()) {
    result.state = txn::TransactionState::kAborted;
    context.MarkAborted();
    result.operations.push_back(std::move(op_result));
    return;
  }

  KeyLockGuard key_guard{lock_manager_, operation.key, lock::LockMode::kExclusive};
  std::unique_lock tree_guard{btree_mutex_};
  btree_.Insert(operation.key, *operation.value);
  op_result.success = true;
  op_result.value = operation.value;
  context.Write(operation.key, *operation.value);

  result.operations.push_back(std::move(op_result));
}

void Worker::ApplyDelete(const txn::Operation& operation, TransactionResult& result) {
  OperationResult op_result{};
  op_result.type = operation.type;
  op_result.key = operation.key;

  KeyLockGuard key_guard{lock_manager_, operation.key, lock::LockMode::kExclusive};
  std::unique_lock tree_guard{btree_mutex_};
  op_result.success = btree_.Erase(operation.key);

  result.operations.push_back(std::move(op_result));
}

} // namespace jubilant::server
