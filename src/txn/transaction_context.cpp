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
  const auto record_iter = overlay_.find(key);
  if (record_iter != overlay_.end()) {
    return record_iter->second;
  }
  return std::nullopt;
}

std::optional<storage::btree::Record> TransactionContext::ReadThrough(
    const std::string& key,
    const std::function<std::optional<storage::btree::Record>()>& storage_reader) {
  if (const auto overlay_value = Read(key); overlay_value.has_value() || HasOverlayEntry(key)) {
    return overlay_value;
  }

  auto storage_value = storage_reader ? storage_reader() : std::nullopt;
  if (storage_value.has_value()) {
    overlay_.insert_or_assign(key, storage_value);
  }
  return storage_value;
}

void TransactionContext::Write(const std::string& key, storage::btree::Record record) {
  overlay_.insert_or_assign(key, std::move(record));
}

void TransactionContext::StageDelete(const std::string& key) {
  overlay_.insert_or_assign(key, std::nullopt);
}

bool TransactionContext::HasOverlayEntry(const std::string& key) const noexcept {
  return overlay_.find(key) != overlay_.end();
}

bool TransactionContext::IsDeleted(const std::string& key) const noexcept {
  const auto iter = overlay_.find(key);
  return iter != overlay_.end() && !iter->second.has_value();
}

void TransactionContext::MarkCommitted() {
  state_ = TransactionState::kCommitted;
}

void TransactionContext::MarkAborted() {
  state_ = TransactionState::kAborted;
}

} // namespace jubilant::txn
