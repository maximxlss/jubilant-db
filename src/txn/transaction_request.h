#pragma once

#include "lock/lock_manager.h"
#include "storage/btree/btree.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace jubilant::txn {

// NOTE: ASSERT_* operations are validated and parsed but execution paths currently abort until
// transaction-context evaluation and worker locking semantics land.
enum class OperationType : std::uint8_t {
  kGet,
  kSet,
  kDelete,
  kAssertExists,
  kAssertNotExists,
  kAssertType,
  kAssertIntEq,
  kAssertBytesHashEq,
  kAssertStringHashEq,
};

struct AssertExpectation {
  std::optional<storage::btree::ValueType> expected_type;
  std::optional<std::int64_t> expected_int;
  std::optional<std::string> expected_hash;
};

struct KeySpec {
  std::uint32_t id{0};
  lock::LockMode mode{lock::LockMode::kShared};
  std::string key;
};

struct Operation {
  OperationType type{OperationType::kGet};
  std::uint32_t key_id{0};
  std::string key;
  std::optional<storage::btree::Record> value;
  std::optional<AssertExpectation> expected;
};

struct TransactionRequest {
  std::uint64_t id{0};
  std::vector<KeySpec> keys;
  std::vector<Operation> operations;

  [[nodiscard]] bool Valid() const;
  [[nodiscard]] const KeySpec* FindKey(std::uint32_t key_id) const;
  [[nodiscard]] std::optional<std::string> ResolveKey(const Operation& operation) const;
};

[[nodiscard]] lock::LockMode LockModeForOperation(OperationType type) noexcept;
TransactionRequest BuildTransactionRequest(std::uint64_t txn_id, std::vector<Operation> operations);
TransactionRequest BuildTransactionRequest(std::vector<Operation> operations);

} // namespace jubilant::txn
