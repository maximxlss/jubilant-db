#include <flatbuffers/flatbuffers.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "disk_generated.h"
#include "meta/manifest.h"

using jubilant::meta::ManifestRecord;
using jubilant::meta::ManifestStore;

namespace fs = std::filesystem;

namespace {

fs::path TempDir(const std::string& name) {
  const auto dir = fs::temp_directory_path() / name;
  fs::remove_all(dir);
  return dir;
}

}  // namespace

TEST(ManifestStoreTest, PersistsAndLoadsValidManifest) {
  const auto dir = TempDir("jubilant-manifest-valid");
  ManifestStore store{dir};

  auto manifest = store.NewDefault("uuid-123");
  manifest.page_size = 8192;
  manifest.inline_threshold = 512;

  ASSERT_TRUE(store.Persist(manifest));

  const auto loaded = store.Load();
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->db_uuid, "uuid-123");
  EXPECT_EQ(loaded->page_size, 8192u);
  EXPECT_EQ(loaded->inline_threshold, 512u);
  EXPECT_EQ(loaded->hash_algorithm, manifest.hash_algorithm);
}

TEST(ManifestStoreTest, RejectsInvalidManifestValues) {
  const auto dir = TempDir("jubilant-manifest-invalid");
  ManifestStore store{dir};

  auto manifest = store.NewDefault("uuid-456");
  manifest.inline_threshold = manifest.page_size;  // Not allowed to inline full page.

  EXPECT_FALSE(store.Persist(manifest));

  manifest.inline_threshold = 0;
  EXPECT_FALSE(store.Persist(manifest));

  manifest.inline_threshold = 1024;
  manifest.hash_algorithm.clear();
  EXPECT_FALSE(store.Persist(manifest));
}

TEST(ManifestStoreTest, LoadRejectsInvalidManifestOnDisk) {
  const auto dir = TempDir("jubilant-manifest-load");

  // Write an invalid manifest with format_major == 0 to disk.
  flatbuffers::FlatBufferBuilder builder;
  const auto uuid_vec = builder.CreateVector(reinterpret_cast<const uint8_t*>("bad-uuid"), 8);
  const auto wire_schema = builder.CreateString("wire");
  const auto disk_schema = builder.CreateString("disk");
  const auto wal_schema = builder.CreateString("wal");
  const auto hash_algorithm = builder.CreateString("sha256");

  const auto manifest_offset = jubilant::disk::CreateManifest(
      builder,
      /*format_major=*/0,
      /*format_minor=*/0,
      /*page_size=*/4096,
      /*inline_threshold=*/1024, uuid_vec, wire_schema, disk_schema, wal_schema, hash_algorithm);

  builder.FinishSizePrefixed(manifest_offset, jubilant::disk::ManifestIdentifier());

  const auto manifest_path = dir / "MANIFEST";
  fs::create_directories(dir);
  std::ofstream out(manifest_path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(out);
  out.write(reinterpret_cast<const char*>(builder.GetBufferPointer()), builder.GetSize());
  out.close();

  ManifestStore store{dir};
  EXPECT_FALSE(store.Load().has_value());
}
