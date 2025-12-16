#include "txn/transaction_context.h"

#include <utility>

namespace jubilant::txn {

TransactionContext::TransactionContext(std::uint64_t id) : id_(id) {}

std::uint64_t TransactionContext::id() const noexcept { return id_; }

TransactionState TransactionContext::state() const noexcept { return state_; }

std::optional<storage::btree::Record> TransactionContext::Read(
    const std::string& key) const {
  auto it = overlay_.find(key);
  if (it != overlay_.end()) {
    return it->second;
  }
  return std::nullopt;
}

void TransactionContext::Write(const std::string& key,
                               storage::btree::Record record) {
  overlay_.insert_or_assign(key, std::move(record));
}

void TransactionContext::MarkCommitted() { state_ = TransactionState::kCommitted; }

void TransactionContext::MarkAborted() { state_ = TransactionState::kAborted; }

}  // namespace jubilant::txn
