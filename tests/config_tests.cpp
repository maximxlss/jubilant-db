#include "config/config.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string_view>

namespace jubilant::config {
namespace {

std::filesystem::path WriteTempConfig(const std::filesystem::path& name, std::string_view contents) {
  const auto temp_dir = std::filesystem::temp_directory_path() / "jubildb_config_tests";
  std::filesystem::create_directories(temp_dir);

  const auto config_path = temp_dir / name;
  std::ofstream file(config_path);
  file << contents;
  file.close();

  return config_path;
}

} // namespace

TEST(ConfigLoaderTest, LoadsAllFields) {
  const auto path = WriteTempConfig("full.toml",
                                    R"(db_path = "./data"
page_size = 8192
inline_threshold = 2048
group_commit_max_latency_ms = 12
cache_bytes = 134217728
listen_address = "0.0.0.0"
listen_port = 7777
)");

  const auto cfg = ConfigLoader::LoadFromFile(path);
  ASSERT_TRUE(cfg.has_value());
  if (!cfg.has_value()) {
    return;
  }

  const auto& loaded = cfg.value();

  EXPECT_EQ(loaded.db_path, std::filesystem::path("./data"));
  EXPECT_EQ(loaded.page_size, 8192U);
  EXPECT_EQ(loaded.inline_threshold, 2048U);
  EXPECT_EQ(loaded.group_commit_max_latency_ms, 12U);
  EXPECT_EQ(loaded.cache_bytes, 134217728ULL);
  EXPECT_EQ(loaded.listen_address, "0.0.0.0");
  EXPECT_EQ(loaded.listen_port, 7777);
}

TEST(ConfigLoaderTest, FallsBackToDefaults) {
  const auto path = WriteTempConfig("defaults.toml", "db_path = \"/var/lib/jubildb\"\n");

  const auto cfg = ConfigLoader::LoadFromFile(path);
  ASSERT_TRUE(cfg.has_value());
  if (!cfg.has_value()) {
    return;
  }

  const auto& loaded = cfg.value();

  EXPECT_EQ(loaded.db_path, std::filesystem::path("/var/lib/jubildb"));
  EXPECT_EQ(loaded.page_size, 4096U);
  EXPECT_EQ(loaded.inline_threshold, 1024U);
  EXPECT_EQ(loaded.group_commit_max_latency_ms, 5U);
  EXPECT_EQ(loaded.cache_bytes, 64U * 1024U * 1024U);
  EXPECT_EQ(loaded.listen_address, "127.0.0.1");
  EXPECT_EQ(loaded.listen_port, 6767);
}

TEST(ConfigLoaderTest, RejectsInvalidInlineThreshold) {
  const auto path = WriteTempConfig("invalid.toml", "db_path = \"./data\"\ninline_threshold = 0\n");

  const auto cfg = ConfigLoader::LoadFromFile(path);
  EXPECT_FALSE(cfg.has_value());
}

} // namespace jubilant::config
