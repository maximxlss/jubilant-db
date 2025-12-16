#include "storage/btree/btree.h"

#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <variant>
#include <vector>

using jubilant::storage::btree::BTree;
using jubilant::storage::btree::Record;

TEST(BTreeTest, InsertAndFindReturnsStoredRecord) {
  BTree tree;
  Record record{};
  record.value = std::string{"hello"};
  record.metadata.ttl_epoch_seconds = 1234;

  tree.Insert("key", record);

  const auto found = tree.Find("key");
  ASSERT_TRUE(found.has_value());
  if (!found.has_value()) {
    return;
  }
  const auto& found_record = found.value();
  EXPECT_EQ(std::get<std::string>(found_record.value), "hello");
  EXPECT_EQ(found_record.metadata.ttl_epoch_seconds, 1234U);
}

TEST(BTreeTest, InsertOverwritesExistingKey) {
  BTree tree;
  Record first{};
  first.value = std::int64_t{7};
  first.metadata.ttl_epoch_seconds = 10;

  tree.Insert("dup", first);

  Record second{};
  second.value = std::vector<std::byte>{std::byte{0xAA}};
  second.metadata.ttl_epoch_seconds = 20;

  tree.Insert("dup", second);

  const auto found = tree.Find("dup");
  ASSERT_TRUE(found.has_value());
  if (!found.has_value()) {
    return;
  }
  const auto& found_record = found.value();
  EXPECT_EQ(std::get<std::vector<std::byte>>(found_record.value).front(), std::byte{0xAA});
  EXPECT_EQ(found_record.metadata.ttl_epoch_seconds, 20U);
  EXPECT_EQ(tree.size(), 1U);
}

TEST(BTreeTest, EraseRemovesKeysAndReportsResult) {
  BTree tree;
  Record record{};
  record.value = std::string{"value"};

  tree.Insert("key", record);
  EXPECT_TRUE(tree.Erase("key"));
  EXPECT_FALSE(tree.Find("key").has_value());
  EXPECT_FALSE(tree.Erase("key"));
}
