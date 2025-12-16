#include "storage/btree/btree.h"
#include "storage/pager/pager.h"
#include "storage/vlog/value_log.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>
#include <variant>
#include <vector>

using jubilant::storage::Pager;
using jubilant::storage::btree::BTree;
using jubilant::storage::btree::Record;
using jubilant::storage::vlog::ValueLog;

namespace {

std::filesystem::path TempDir(const std::string& name) {
  const auto dir = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(dir);
  return dir;
}

} // namespace

TEST(BTreeTest, InsertAndFindReturnsStoredRecord) {
  const auto dir = TempDir("jubilant-btree-inline");
  Pager pager = Pager::Open(dir / "data.pages", jubilant::storage::kDefaultPageSize);
  ValueLog vlog(dir / "vlog");
  BTree tree(
      BTree::Config{.pager = &pager, .value_log = &vlog, .inline_threshold = 128U, .root_hint = 0});
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
  const auto dir = TempDir("jubilant-btree-overwrite");
  Pager pager = Pager::Open(dir / "data.pages", jubilant::storage::kDefaultPageSize);
  ValueLog vlog(dir / "vlog");
  BTree tree(
      BTree::Config{.pager = &pager, .value_log = &vlog, .inline_threshold = 128U, .root_hint = 0});
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

TEST(BTreeTest, RoutesLargeValuesToValueLog) {
  const auto dir = TempDir("jubilant-btree-vlog");
  Pager pager = Pager::Open(dir / "data.pages", jubilant::storage::kDefaultPageSize);
  ValueLog vlog(dir / "vlog");
  BTree tree(
      BTree::Config{.pager = &pager, .value_log = &vlog, .inline_threshold = 8U, .root_hint = 0});
  Record record{};
  record.value = std::string(32, 'x');

  tree.Insert("big", record);

  const auto found = tree.Find("big");
  ASSERT_TRUE(found.has_value());
  if (!found.has_value()) {
    return;
  }

  const auto& found_record = found.value();
  EXPECT_EQ(std::get<std::string>(found_record.value).size(), 32U);
}

TEST(BTreeTest, PersistsAcrossReload) {
  const auto dir = TempDir("jubilant-btree-reload");
  {
    Pager pager = Pager::Open(dir / "data.pages", jubilant::storage::kDefaultPageSize);
    ValueLog vlog(dir / "vlog");
    BTree tree(BTree::Config{
        .pager = &pager, .value_log = &vlog, .inline_threshold = 16U, .root_hint = 0});

    Record record{};
    record.value = std::vector<std::byte>{std::byte{0xAA}, std::byte{0xBB}};
    tree.Insert("persisted", record);
  }

  Pager pager = Pager::Open(dir / "data.pages", jubilant::storage::kDefaultPageSize);
  ValueLog vlog(dir / "vlog");
  BTree reloaded(
      BTree::Config{.pager = &pager, .value_log = &vlog, .inline_threshold = 16U, .root_hint = 0});

  const auto found = reloaded.Find("persisted");
  ASSERT_TRUE(found.has_value());
  if (!found.has_value()) {
    return;
  }
  EXPECT_EQ(std::get<std::vector<std::byte>>(found->value)[1], std::byte{0xBB});
}

TEST(BTreeTest, EraseRemovesKeysAndReportsResult) {
  const auto dir = TempDir("jubilant-btree-erase");
  Pager pager = Pager::Open(dir / "data.pages", jubilant::storage::kDefaultPageSize);
  ValueLog vlog(dir / "vlog");
  BTree tree(
      BTree::Config{.pager = &pager, .value_log = &vlog, .inline_threshold = 128U, .root_hint = 0});
  Record record{};
  record.value = std::string{"value"};

  tree.Insert("key", record);
  EXPECT_TRUE(tree.Erase("key"));
  EXPECT_FALSE(tree.Find("key").has_value());
  EXPECT_FALSE(tree.Erase("key"));
}
