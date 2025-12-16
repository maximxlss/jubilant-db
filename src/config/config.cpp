#include "config/config.h"

#include <utility>

namespace jubilant::config {

auto ConfigLoader::Default(std::filesystem::path db_path) -> Config {
  Config cfg{};
  cfg.db_path = std::move(db_path);
  return cfg;
}

auto ConfigLoader::LoadFromFile(const std::filesystem::path& /*path*/) -> std::optional<Config> {
  // TOML parsing will be added once configuration is introduced. Returning
  // nullopt allows callers to fall back to defaults while keeping the API
  // stable for tests.
  return std::nullopt;
}

}  // namespace jubilant::config
