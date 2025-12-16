#include "storage/btree/btree.h"
#include "storage/simple_store.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <variant>

using jubilant::storage::SimpleStore;
using jubilant::storage::btree::Record;

namespace fs = std::filesystem;

namespace {

fs::path TempDir(const std::string& name) {
  const auto dir = fs::temp_directory_path() / name;
  fs::remove_all(dir);
  return dir;
}

} // namespace

TEST(SimpleStoreTest, SetGetAndDelete) {
  const auto dir = TempDir("jubilant-simple-store-1");
  auto store = SimpleStore::Open(dir);

  Record record{};
  record.value = std::string{"value"};

  store.Set("key", record);
  const auto found = store.Get("key");
  ASSERT_TRUE(found.has_value());
  if (!found.has_value()) {
    return;
  }
  const auto& found_record = found.value();
  EXPECT_EQ(std::get<std::string>(found_record.value), "value");

  EXPECT_TRUE(store.Delete("key"));
  EXPECT_FALSE(store.Get("key").has_value());
}

TEST(SimpleStoreTest, RejectsEmptyKeys) {
  const auto dir = TempDir("jubilant-simple-store-empty-key");
  auto store = SimpleStore::Open(dir);

  Record record{};
  record.value = std::string{"value"};

  EXPECT_THROW(store.Set("", record), std::invalid_argument);
  EXPECT_THROW(store.Delete(""), std::invalid_argument);
}

TEST(SimpleStoreTest, DeleteMissingKeyDoesNotWriteTombstone) {
  const auto dir = TempDir("jubilant-simple-store-tombstone");
  auto store = SimpleStore::Open(dir);

  const auto data_file = dir / "data.pages";
  const auto initial_size =
      std::filesystem::exists(data_file) ? std::filesystem::file_size(data_file) : 0U;

  EXPECT_FALSE(store.Delete("absent"));
  store.Sync();

  EXPECT_TRUE(std::filesystem::exists(data_file));
  EXPECT_EQ(std::filesystem::file_size(data_file), initial_size);
}

TEST(SimpleStoreTest, PersistsAcrossReopen) {
  const auto dir = TempDir("jubilant-simple-store-2");
  {
    auto store = SimpleStore::Open(dir);
    Record record{};
    record.value = std::int64_t{42};
    store.Set("answer", record);
    store.Sync();
  }

  auto reopened = SimpleStore::Open(dir);
  const auto found = reopened.Get("answer");
  ASSERT_TRUE(found.has_value());
  if (!found.has_value()) {
    return;
  }
  const auto& found_record = found.value();
  EXPECT_EQ(std::get<std::int64_t>(found_record.value), 42);
}

TEST(SimpleStoreTest, RoutesLargeValuesThroughValueLogAndReloads) {
  const auto dir = TempDir("jubilant-simple-store-vlog");
  std::string large_value(2048, 'z');
  {
    auto store = SimpleStore::Open(dir);
    Record record{};
    record.value = large_value;
    store.Set("big", record);
    store.Sync();
  }

  const auto segment_path = dir / "vlog" / "segment-0.vlog";
  EXPECT_TRUE(std::filesystem::exists(segment_path));

  auto reopened = SimpleStore::Open(dir);
  const auto found = reopened.Get("big");
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(std::get<std::string>(found->value), large_value);
}

TEST(SimpleStoreTest, ReportsStatsWithMetadataAndCounts) {
  const auto dir = TempDir("jubilant-simple-store-stats");
  auto store = SimpleStore::Open(dir);

  Record record{};
  record.value = std::int64_t{7};
  store.Set("alpha", record);
  record.value = std::string{"bravo"};
  store.Set("bravo", record);

  const auto stats = store.stats();
  EXPECT_FALSE(stats.manifest.db_uuid.empty());
  EXPECT_GT(stats.manifest.page_size, 0U);
  EXPECT_GE(stats.superblock.root_page_id, 0U);
  EXPECT_GE(stats.page_count, 1U);
  EXPECT_EQ(stats.key_count, 2U);
}

TEST(SimpleStoreTest, ValidateOnDiskDetectsMissingManifest) {
  const auto dir = TempDir("jubilant-simple-store-validation-missing");
  const auto report = SimpleStore::ValidateOnDisk(dir);

  EXPECT_FALSE(report.ok);
  EXPECT_FALSE(report.manifest_result.ok);
  EXPECT_FALSE(report.superblock_ok);
}

TEST(SimpleStoreTest, ValidateOnDiskSucceedsAfterOpen) {
  const auto dir = TempDir("jubilant-simple-store-validation-ok");
  auto store = SimpleStore::Open(dir);
  store.Sync();

  const auto report = SimpleStore::ValidateOnDisk(dir);
  EXPECT_TRUE(report.ok);
  EXPECT_TRUE(report.manifest_result.ok);
  EXPECT_TRUE(report.superblock_ok);
  EXPECT_TRUE(report.checkpoint_ok);
}
