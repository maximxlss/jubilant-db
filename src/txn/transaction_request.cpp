#include "txn/transaction_request.h"

namespace jubilant::txn {

bool TransactionRequest::Valid() const {
  if (operations.empty()) {
    return false;
  }

  for (const auto& operation : operations) {
    if (operation.key.empty()) {
      return false;
    }
    if (operation.type == OperationType::kSet && !operation.value.has_value()) {
      return false;
    }
  }

  return true;
}

} // namespace jubilant::txn
