#pragma once

#include "storage/btree/btree.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace jubilant::txn {

enum class OperationType { kGet, kSet, kDelete };

struct Operation {
  OperationType type{OperationType::kGet};
  std::string key;
  std::optional<storage::btree::Record> value;
};

struct TransactionRequest {
  std::uint64_t id{0};
  std::vector<Operation> operations;

  [[nodiscard]] bool Valid() const;
};

} // namespace jubilant::txn
