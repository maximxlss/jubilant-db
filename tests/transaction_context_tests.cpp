#include "storage/btree/btree.h"
#include "txn/transaction_context.h"

#include <gtest/gtest.h>
#include <optional>
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

TEST(TransactionContextTest, ReadThroughCachesStorageValues) {
  TransactionContext txn{101};
  int storage_reads = 0;

  const auto loader = [&]() -> std::optional<Record> {
    ++storage_reads;
    Record record{};
    record.value = std::string{"cached"};
    return record;
  };

  const auto first = txn.ReadThrough("key", loader);
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(storage_reads, 1);

  const auto second = txn.ReadThrough("key", loader);
  ASSERT_TRUE(second.has_value());
  if (!second.has_value()) {
    return;
  }
  EXPECT_EQ(storage_reads, 1);
  EXPECT_EQ(std::get<std::string>(second->value), "cached");
}

TEST(TransactionContextTest, StageDeleteTracksTombstones) {
  TransactionContext txn{202};
  txn.StageDelete("gone");

  EXPECT_TRUE(txn.HasOverlayEntry("gone"));
  EXPECT_TRUE(txn.IsDeleted("gone"));
  EXPECT_FALSE(txn.Read("gone").has_value());

  bool storage_checked = false;
  const auto storage_loader = [&]() -> std::optional<Record> {
    storage_checked = true;
    return std::nullopt;
  };
  const auto read_back = txn.ReadThrough("gone", storage_loader);
  EXPECT_FALSE(read_back.has_value());
  EXPECT_FALSE(storage_checked);
}
