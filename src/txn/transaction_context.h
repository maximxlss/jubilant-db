#pragma once

#include "storage/btree/btree.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

namespace jubilant::txn {

enum class TransactionState : std::uint8_t { kPending, kCommitted, kAborted };

class TransactionContext {
public:
  explicit TransactionContext(std::uint64_t transaction_id);

  [[nodiscard]] std::uint64_t id() const noexcept;
  [[nodiscard]] TransactionState state() const noexcept;

  [[nodiscard]] std::optional<storage::btree::Record> Read(const std::string& key) const;
  [[nodiscard]] std::optional<storage::btree::Record>
  ReadThrough(const std::string& key,
              const std::function<std::optional<storage::btree::Record>()>& storage_reader);
  void Write(const std::string& key, storage::btree::Record record);
  void StageDelete(const std::string& key);
  [[nodiscard]] bool HasOverlayEntry(const std::string& key) const noexcept;
  [[nodiscard]] bool IsDeleted(const std::string& key) const noexcept;
  void MarkCommitted();
  void MarkAborted();

private:
  std::uint64_t id_;
  TransactionState state_{TransactionState::kPending};
  std::unordered_map<std::string, std::optional<storage::btree::Record>> overlay_;
};

} // namespace jubilant::txn
