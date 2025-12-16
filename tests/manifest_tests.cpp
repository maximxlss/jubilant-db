#include "disk_generated.h"
#include "meta/manifest.h"

#include <filesystem>
#include <flatbuffers/flatbuffers.h>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using jubilant::meta::ManifestRecord;
using jubilant::meta::ManifestStore;

namespace fs = std::filesystem;

namespace {

fs::path TempDir(const std::string& name) {
  const auto dir = fs::temp_directory_path() / name;
  fs::remove_all(dir);
  return dir;
}

} // namespace

TEST(ManifestStoreTest, PersistsAndLoadsValidManifest) {
  const auto dir = TempDir("jubilant-manifest-valid");
  ManifestStore store{dir};

  auto manifest = jubilant::meta::ManifestStore::NewDefault("uuid-123");
  manifest.page_size = 8192;
  manifest.inline_threshold = 512;

  ASSERT_TRUE(store.Persist(manifest));

  const auto loaded = store.Load();
  ASSERT_TRUE(loaded.has_value());
  if (!loaded.has_value()) {
    return;
  }
  const auto& loaded_manifest = loaded.value();
  EXPECT_EQ(loaded_manifest.generation, 1U);
  EXPECT_EQ(loaded_manifest.db_uuid, "uuid-123");
  EXPECT_EQ(loaded_manifest.page_size, 8192U);
  EXPECT_EQ(loaded_manifest.inline_threshold, 512U);
  EXPECT_EQ(loaded_manifest.hash_algorithm, manifest.hash_algorithm);
}

TEST(ManifestStoreTest, RejectsInvalidManifestValues) {
  const auto dir = TempDir("jubilant-manifest-invalid");
  ManifestStore store{dir};

  auto manifest = jubilant::meta::ManifestStore::NewDefault("uuid-456");
  manifest.inline_threshold = manifest.page_size; // Not allowed to inline full page.

  EXPECT_FALSE(store.Persist(manifest));

  manifest.inline_threshold = 0;
  EXPECT_FALSE(store.Persist(manifest));

  manifest.inline_threshold = 1024;
  manifest.hash_algorithm.clear();
  EXPECT_FALSE(store.Persist(manifest));
}

TEST(ManifestStoreTest, BumpsGenerationOnRewrite) {
  const auto dir = TempDir("jubilant-manifest-generations");
  ManifestStore store{dir};

  auto manifest = jubilant::meta::ManifestStore::NewDefault("uuid-gen");
  ASSERT_TRUE(store.Persist(manifest));
  EXPECT_EQ(manifest.generation, 1U);

  manifest.inline_threshold = 512;
  ASSERT_TRUE(store.Persist(manifest));

  const auto loaded = store.Load();
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->generation, 2U);
  EXPECT_EQ(loaded->inline_threshold, 512U);
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
      /*generation=*/1,
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
