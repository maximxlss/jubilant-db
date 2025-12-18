#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace jubilant::meta {

struct ManifestRecord {
  std::uint64_t generation{1};
  std::uint16_t format_major{1};
  std::uint16_t format_minor{0};
  // Page size (bytes) for data.pages. The pager and B+Tree leaf layout rely on this staying stable
  // across restarts so WAL replay can validate page boundaries.
  std::uint32_t page_size{4096};
  // Inline value threshold (bytes). Values above this spill into the value log and store a shared
  // SegmentPointer {segment_id, offset, length} instead of inline bytes. Persisted here so WAL,
  // value log, and B+Tree encode/decode decisions stay consistent.
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
  explicit ManifestStore(const std::filesystem::path& base_dir);

  [[nodiscard]] static ManifestRecord NewDefault(std::string uuid_seed);
  [[nodiscard]] std::optional<ManifestRecord> Load() const;
  [[nodiscard]] static ManifestValidationResult Validate(const ManifestRecord& manifest);
  bool Persist(ManifestRecord& manifest);

private:
  std::filesystem::path manifest_path_;
};

} // namespace jubilant::meta
