#include "txn/transaction_context.h"

#include <utility>

namespace jubilant::txn {

TransactionContext::TransactionContext(std::uint64_t transaction_id) : id_(transaction_id) {}

auto TransactionContext::id() const noexcept -> std::uint64_t { return id_; }

auto TransactionContext::state() const noexcept -> TransactionState { return state_; }

auto TransactionContext::Read(const std::string& key) const
    -> std::optional<storage::btree::Record> {
  auto overlay_iter = overlay_.find(key);
  if (overlay_iter != overlay_.end()) {
    return overlay_iter->second;
  }
  return std::nullopt;
}

void TransactionContext::Write(const std::string& key, storage::btree::Record record) {
  overlay_.insert_or_assign(key, std::move(record));
}

void TransactionContext::MarkCommitted() { state_ = TransactionState::kCommitted; }

void TransactionContext::MarkAborted() { state_ = TransactionState::kAborted; }

}  // namespace jubilant::txn
