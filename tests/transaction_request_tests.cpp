#include "lock/lock_manager.h"
#include "txn/transaction_request.h"

#include <gtest/gtest.h>
#include <string>

using jubilant::lock::LockMode;
using jubilant::storage::btree::Record;
using jubilant::txn::BuildTransactionRequest;
using jubilant::txn::Operation;
using jubilant::txn::OperationType;
using jubilant::txn::TransactionRequest;

TEST(TransactionRequestTest, BuildsKeyTableAndValidates) {
  Record record{};
  record.value = std::string{"value"};

  Operation set_op{.type = OperationType::kSet, .key = "alpha", .value = record};
  Operation get_op{.type = OperationType::kGet, .key = "alpha"};

  TransactionRequest request = BuildTransactionRequest(9, {set_op, get_op});

  ASSERT_TRUE(request.Valid());
  ASSERT_EQ(request.keys.size(), 1U);
  EXPECT_EQ(request.keys.front().key, "alpha");
  EXPECT_EQ(request.keys.front().mode, LockMode::kExclusive);
  ASSERT_EQ(request.operations.size(), 2U);
  EXPECT_EQ(request.operations[0].key_id, request.operations[1].key_id);
}

TEST(TransactionRequestTest, UsesSharedLockForReads) {
  Operation read_op{.type = OperationType::kGet, .key = "beta"};
  TransactionRequest request = BuildTransactionRequest(0, {read_op});

  ASSERT_TRUE(request.Valid());
  ASSERT_EQ(request.keys.size(), 1U);
  EXPECT_EQ(request.keys.front().mode, LockMode::kShared);
  EXPECT_EQ(request.operations.front().key_id, 0U);
}

TEST(TransactionRequestTest, RejectsWeakerDeclaredLockModes) {
  Record record{};
  record.value = std::string{"value"};

  TransactionRequest request{};
  request.id = 11;
  request.keys.push_back(
      jubilant::txn::KeySpec{.id = 0, .mode = LockMode::kShared, .key = "alpha"});
  request.operations.push_back(
      Operation{.type = OperationType::kSet, .key_id = 0, .key = "alpha", .value = record});

  EXPECT_FALSE(request.Valid());
}
