#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace jubilant::config {

struct Config {
  std::filesystem::path db_path;
  std::uint32_t page_size{4096};
  std::uint32_t inline_threshold{1024};
  std::uint32_t group_commit_max_latency_ms{5};
  std::uint64_t cache_bytes{64 * 1024 * 1024};
  std::string listen_address{"127.0.0.1"};
  std::uint16_t listen_port{6767};
};

class ConfigLoader {
 public:
  [[nodiscard]] static Config Default(std::filesystem::path db_path);
  [[nodiscard]] static std::optional<Config> LoadFromFile(const std::filesystem::path& path);
};

}  // namespace jubilant::config
