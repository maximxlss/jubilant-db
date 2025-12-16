#include "config/config.h"

#include <utility>

namespace jubilant::config {

Config ConfigLoader::Default(std::filesystem::path db_path) {
  Config cfg{};
  cfg.db_path = std::move(db_path);
  return cfg;
}

std::optional<Config> ConfigLoader::LoadFromFile(const std::filesystem::path& /*path*/) {
  // TOML parsing will be added once configuration is introduced. Returning
  // nullopt allows callers to fall back to defaults while keeping the API
  // stable for tests.
  return std::nullopt;
}

} // namespace jubilant::config
