#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace jubilant::meta {

struct ManifestRecord {
  std::uint16_t format_major{1};
  std::uint16_t format_minor{0};
  std::uint32_t page_size{4096};
  std::uint32_t inline_threshold{1024};
  std::string db_uuid;
  std::string wire_schema;
  std::string disk_schema;
  std::string wal_schema;
  std::string hash_algorithm{"sha256"};
};

struct ManifestValidationResult {
  bool ok{false};
  std::string message;
};

class ManifestStore {
 public:
  explicit ManifestStore(std::filesystem::path base_dir);

  [[nodiscard]] ManifestRecord NewDefault(std::string uuid_seed) const;
  [[nodiscard]] std::optional<ManifestRecord> Load() const;
  [[nodiscard]] ManifestValidationResult Validate(
      const ManifestRecord& manifest) const;
  bool Persist(const ManifestRecord& manifest);

 private:
  std::filesystem::path manifest_path_;
};

}  // namespace jubilant::meta
