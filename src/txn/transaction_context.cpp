#include "txn/transaction_context.h"

#include <utility>

namespace jubilant::txn {

TransactionContext::TransactionContext(std::uint64_t transaction_id) : id_(transaction_id) {}

std::uint64_t TransactionContext::id() const noexcept {
  return id_;
}

TransactionState TransactionContext::state() const noexcept {
  return state_;
}

std::optional<storage::btree::Record> TransactionContext::Read(const std::string& key) const {
  auto record_iter = overlay_.find(key);
  if (record_iter != overlay_.end()) {
    return record_iter->second;
  }
  return std::nullopt;
}

void TransactionContext::Write(const std::string& key, storage::btree::Record record) {
  overlay_.insert_or_assign(key, std::move(record));
}

void TransactionContext::MarkCommitted() {
  state_ = TransactionState::kCommitted;
}

void TransactionContext::MarkAborted() {
  state_ = TransactionState::kAborted;
}

} // namespace jubilant::txn
