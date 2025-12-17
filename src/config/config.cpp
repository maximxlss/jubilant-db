#include "config/config.h"

#include <limits>
#include <toml++/toml.h>
#include <utility>

namespace jubilant::config {

Config ConfigLoader::Default(std::filesystem::path db_path) {
  Config cfg{};
  cfg.db_path = std::move(db_path);
  return cfg;
}

std::optional<Config> ConfigLoader::LoadFromFile(const std::filesystem::path& path) {
  toml::table table;
  try {
    table = toml::parse_file(path.string());
  } catch (const toml::parse_error&) {
    return std::nullopt;
  }

  Config cfg{};

  const auto db_path = table["db_path"];
  const auto db_path_value = db_path.value<std::string>();
  if (!db_path || !db_path.is_string() || !db_path_value || db_path_value->empty()) {
    return std::nullopt;
  }
  cfg.db_path = *db_path_value;

  if (const auto page_size = table["page_size"].value<std::uint32_t>()) {
    cfg.page_size = *page_size;
  }

  if (const auto inline_threshold = table["inline_threshold"].value<std::uint32_t>()) {
    cfg.inline_threshold = *inline_threshold;
  }

  if (const auto group_commit_latency =
          table["group_commit_max_latency_ms"].value<std::uint32_t>()) {
    cfg.group_commit_max_latency_ms = *group_commit_latency;
  }

  if (const auto cache_bytes = table["cache_bytes"].value<std::uint64_t>()) {
    cfg.cache_bytes = *cache_bytes;
  }

  if (const auto listen_address = table["listen_address"].value<std::string>()) {
    if (listen_address->empty()) {
      return std::nullopt;
    }
    cfg.listen_address = *listen_address;
  }

  if (const auto listen_port = table["listen_port"].value<std::uint32_t>()) {
    if (*listen_port == 0 || *listen_port > std::numeric_limits<std::uint16_t>::max()) {
      return std::nullopt;
    }
    cfg.listen_port = static_cast<std::uint16_t>(*listen_port);
  }

  if (cfg.page_size == 0) {
    return std::nullopt;
  }

  if (cfg.inline_threshold == 0 || cfg.inline_threshold >= cfg.page_size) {
    return std::nullopt;
  }

  if (cfg.group_commit_max_latency_ms == 0) {
    return std::nullopt;
  }

  if (cfg.cache_bytes == 0) {
    return std::nullopt;
  }

  return cfg;
}

} // namespace jubilant::config
