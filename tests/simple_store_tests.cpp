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
