#include "storage/btree/btree.h"
#include "txn/transaction_context.h"

#include <gtest/gtest.h>
#include <string>
#include <variant>

using jubilant::storage::btree::Record;
using jubilant::txn::TransactionContext;
using jubilant::txn::TransactionState;

TEST(TransactionContextTest, TracksOverlayReadsAndWrites) {
  TransactionContext txn{42};

  EXPECT_EQ(txn.id(), 42U);
  EXPECT_EQ(txn.state(), TransactionState::kPending);
  EXPECT_FALSE(txn.Read("missing").has_value());

  Record record{};
  record.value = std::string{"value"};
  txn.Write("key", record);

  const auto found = txn.Read("key");
  ASSERT_TRUE(found.has_value());
  if (!found.has_value()) {
    return;
  }
  const auto& found_record = found.value();
  EXPECT_EQ(std::get<std::string>(found_record.value), "value");
}

TEST(TransactionContextTest, MarksCommitAndAbortStates) {
  TransactionContext txn{7};

  txn.MarkCommitted();
  EXPECT_EQ(txn.state(), TransactionState::kCommitted);

  txn.MarkAborted();
  EXPECT_EQ(txn.state(), TransactionState::kAborted);
}
