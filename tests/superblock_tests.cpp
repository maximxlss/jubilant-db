#include "meta/superblock.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

using jubilant::meta::SuperBlock;
using jubilant::meta::SuperBlockStore;

namespace fs = std::filesystem;

namespace {

fs::path TempDir(const std::string& name) {
  const auto dir = fs::temp_directory_path() / name;
  fs::remove_all(dir);
  return dir;
}

void CorruptCrc(const fs::path& path) {
  std::fstream stream(path, std::ios::in | std::ios::out | std::ios::binary);
  ASSERT_TRUE(stream.is_open());

  stream.seekp(-static_cast<std::streamoff>(sizeof(std::uint64_t)), std::ios::end);
  const std::uint64_t zero_crc = 0;
  stream.write(reinterpret_cast<const char*>(&zero_crc), sizeof(std::uint64_t));
  stream.flush();
  stream.close();
}

} // namespace

TEST(SuperBlockStoreTest, PicksNewestValidSuperblock) {
  const auto dir = TempDir("jubilant-superblock-valid");
  SuperBlockStore store{dir};

  SuperBlock first{};
  first.root_page_id = 100;
  ASSERT_TRUE(store.WriteNext(first));

  SuperBlock second{};
  second.root_page_id = 200;
  ASSERT_TRUE(store.WriteNext(second));

  const auto active = store.LoadActive();
  ASSERT_TRUE(active.has_value());
  if (!active.has_value()) {
    return;
  }
  EXPECT_EQ(active->generation, 2U);
  EXPECT_EQ(active->root_page_id, 200U);
}

TEST(SuperBlockStoreTest, FallsBackWhenNewerCrcCorrupted) {
  const auto dir = TempDir("jubilant-superblock-crc");
  SuperBlockStore store{dir};

  SuperBlock first{};
  first.root_page_id = 10;
  ASSERT_TRUE(store.WriteNext(first));

  SuperBlock second{};
  second.root_page_id = 20;
  ASSERT_TRUE(store.WriteNext(second));

  CorruptCrc(dir / "SUPERBLOCK_B");

  const auto active = store.LoadActive();
  ASSERT_TRUE(active.has_value());
  if (!active.has_value()) {
    return;
  }
  EXPECT_EQ(active->generation, 1U);
  EXPECT_EQ(active->root_page_id, 10U);
}
