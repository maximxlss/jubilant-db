#include "txn/transaction_request.h"

#include <algorithm>

namespace jubilant::txn {

bool TransactionRequest::Valid() const {
  if (operations.empty()) {
    return false;
  }

  return std::ranges::all_of(operations, [](const Operation& operation) {
    if (operation.key.empty()) {
      return false;
    }
    if (operation.type == OperationType::kSet && !operation.value.has_value()) {
      return false;
    }
    return true;
  });
}

} // namespace jubilant::txn
