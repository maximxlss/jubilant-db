#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

#include "storage/btree/btree.h"

namespace jubilant::txn {

enum class TransactionState { kPending, kCommitted, kAborted };

class TransactionContext {
 public:
  explicit TransactionContext(std::uint64_t id);

  [[nodiscard]] std::uint64_t id() const noexcept;
  [[nodiscard]] TransactionState state() const noexcept;

  [[nodiscard]] std::optional<storage::btree::Record> Read(const std::string& key) const;
  void Write(const std::string& key, storage::btree::Record record);
  void MarkCommitted();
  void MarkAborted();

 private:
  std::uint64_t id_;
  TransactionState state_{TransactionState::kPending};
  std::unordered_map<std::string, storage::btree::Record> overlay_;
};

}  // namespace jubilant::txn
