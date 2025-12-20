#include "txn/transaction_request.h"

#include <algorithm>
#include <map>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

namespace jubilant::txn {

namespace {

[[nodiscard]] bool RequiresValue(OperationType type) {
  switch (type) {
  case OperationType::kSet:
    return true;
  case OperationType::kGet:
  case OperationType::kDelete:
  case OperationType::kAssertExists:
  case OperationType::kAssertNotExists:
  case OperationType::kAssertType:
  case OperationType::kAssertIntEq:
  case OperationType::kAssertBytesHashEq:
  case OperationType::kAssertStringHashEq:
    return false;
  }
  return false;
}

[[nodiscard]] bool RequiresExpectation(OperationType type) {
  switch (type) {
  case OperationType::kAssertType:
  case OperationType::kAssertIntEq:
  case OperationType::kAssertBytesHashEq:
  case OperationType::kAssertStringHashEq:
    return true;
  case OperationType::kGet:
  case OperationType::kSet:
  case OperationType::kDelete:
  case OperationType::kAssertExists:
  case OperationType::kAssertNotExists:
    return false;
  }
  return false;
}

} // namespace

bool TransactionRequest::Valid() const {
  if (operations.empty()) {
    return false;
  }
  if (keys.empty()) {
    return false;
  }

  std::unordered_set<std::uint32_t> unique_ids{};
  for (const auto& key : keys) {
    if (key.key.empty()) {
      return false;
    }
    unique_ids.insert(key.id);
  }
  if (unique_ids.size() != keys.size()) {
    return false;
  }

  return std::ranges::all_of(operations, [this](const Operation& operation) {
    const auto* key_spec = FindKey(operation.key_id);
    if (key_spec == nullptr) {
      return false;
    }
    const auto required_lock_mode = LockModeForOperation(operation.type);
    if (key_spec->mode == lock::LockMode::kShared &&
        required_lock_mode == lock::LockMode::kExclusive) {
      return false;
    }
    if (!operation.key.empty() && operation.key != key_spec->key) {
      return false;
    }
    if (RequiresValue(operation.type) && !operation.value.has_value()) {
      return false;
    }
    if (RequiresExpectation(operation.type) && !operation.expected.has_value()) {
      return false;
    }
    if (!RequiresExpectation(operation.type) && operation.expected.has_value()) {
      return false;
    }
    return true;
  });
}

const KeySpec* TransactionRequest::FindKey(std::uint32_t key_id) const {
  const auto iter = std::find_if(keys.begin(), keys.end(),
                                 [key_id](const KeySpec& key) { return key.id == key_id; });
  if (iter == keys.end()) {
    return nullptr;
  }
  return &*iter;
}

std::optional<std::string> TransactionRequest::ResolveKey(const Operation& operation) const {
  if (const auto* key_spec = FindKey(operation.key_id)) {
    return key_spec->key;
  }
  if (!operation.key.empty()) {
    return operation.key;
  }
  return std::nullopt;
}

lock::LockMode LockModeForOperation(OperationType type) noexcept {
  switch (type) {
  case OperationType::kGet:
  case OperationType::kAssertExists:
  case OperationType::kAssertNotExists:
  case OperationType::kAssertType:
  case OperationType::kAssertIntEq:
  case OperationType::kAssertBytesHashEq:
  case OperationType::kAssertStringHashEq:
    return lock::LockMode::kShared;
  case OperationType::kSet:
  case OperationType::kDelete:
    return lock::LockMode::kExclusive;
  }
  return lock::LockMode::kShared;
}

TransactionRequest BuildTransactionRequest(std::uint64_t txn_id,
                                           std::vector<Operation> operations) {
  TransactionRequest request{};
  request.id = txn_id;

  std::map<std::string, KeySpec> keys_by_name{};

  for (const auto& operation : operations) {
    if (operation.key.empty()) {
      continue;
    }

    const auto lock_mode = LockModeForOperation(operation.type);
    auto [iter, inserted] = keys_by_name.try_emplace(operation.key, KeySpec{});
    if (inserted) {
      iter->second.key = operation.key;
      iter->second.mode = lock_mode;
    } else if (iter->second.mode == lock::LockMode::kShared &&
               lock_mode == lock::LockMode::kExclusive) {
      iter->second.mode = lock::LockMode::kExclusive;
    }
  }

  request.keys.reserve(keys_by_name.size());
  std::unordered_map<std::string, std::uint32_t> id_by_key{};
  id_by_key.reserve(keys_by_name.size());
  for (auto& [name, key_spec] : keys_by_name) {
    key_spec.id = static_cast<std::uint32_t>(request.keys.size());
    id_by_key.emplace(name, key_spec.id);
    request.keys.push_back(key_spec);
  }

  for (auto& operation : operations) {
    if (operation.key.empty()) {
      continue;
    }
    const auto iter = id_by_key.find(operation.key);
    if (iter != id_by_key.end()) {
      operation.key_id = iter->second;
    }
  }

  request.operations = std::move(operations);
  return request;
}

TransactionRequest BuildTransactionRequest(std::vector<Operation> operations) {
  return BuildTransactionRequest(0, std::move(operations));
}

} // namespace jubilant::txn
