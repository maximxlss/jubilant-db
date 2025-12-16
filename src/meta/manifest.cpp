#include "meta/manifest.h"

#include <flatbuffers/flatbuffers.h>

#include <filesystem>
#include <fstream>
#include <vector>
#include <utility>

#include "disk_generated.h"

namespace jubilant::meta {

ManifestStore::ManifestStore(std::filesystem::path base_dir)
    : manifest_path_(std::move(base_dir) / "MANIFEST") {}

ManifestRecord ManifestStore::NewDefault(std::string uuid_seed) const {
  ManifestRecord manifest{};
  manifest.db_uuid = std::move(uuid_seed);
  manifest.wire_schema = "wire-v1";
  manifest.disk_schema = "disk-v1";
  manifest.wal_schema = "wal-v1";
  return manifest;
}

std::optional<ManifestRecord> ManifestStore::Load() const {
  if (!std::filesystem::exists(manifest_path_)) {
    return std::nullopt;
  }

  std::ifstream in(manifest_path_, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }

  std::vector<std::byte> buffer(std::filesystem::file_size(manifest_path_));
  in.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
  if (!in) {
    return std::nullopt;
  }

  auto* manifest_fb = flatbuffers::GetSizePrefixedRoot<disk::Manifest>(
      reinterpret_cast<const uint8_t*>(buffer.data()));
  if (manifest_fb == nullptr) {
    return std::nullopt;
  }

  ManifestRecord record{};
  record.format_major = manifest_fb->format_major();
  record.format_minor = manifest_fb->format_minor();
  record.page_size = manifest_fb->page_size();
  record.inline_threshold = manifest_fb->inline_threshold();

  const auto uuid_vec = manifest_fb->db_uuid();
  if (uuid_vec != nullptr) {
    record.db_uuid.assign(reinterpret_cast<const char*>(uuid_vec->Data()),
                          uuid_vec->size());
  }

  if (manifest_fb->wire_schema() != nullptr) {
    record.wire_schema = manifest_fb->wire_schema()->str();
  }
  if (manifest_fb->disk_schema() != nullptr) {
    record.disk_schema = manifest_fb->disk_schema()->str();
  }
  if (manifest_fb->wal_schema() != nullptr) {
    record.wal_schema = manifest_fb->wal_schema()->str();
  }
  if (manifest_fb->hash_algorithm() != nullptr) {
    record.hash_algorithm = manifest_fb->hash_algorithm()->str();
  }

  return record;
}

ManifestValidationResult ManifestStore::Validate(
    const ManifestRecord& manifest) const {
  ManifestValidationResult result{};
  result.ok = true;

  if (manifest.format_major == 0) {
    result.ok = false;
    result.message = "format_major must be non-zero";
  } else if (manifest.page_size == 0) {
    result.ok = false;
    result.message = "page_size must be non-zero";
  } else if (manifest.db_uuid.empty()) {
    result.ok = false;
    result.message = "db_uuid must be populated";
  }

  return result;
}

bool ManifestStore::Persist(const ManifestRecord& manifest) {
  const auto validation = Validate(manifest);
  if (!validation.ok) {
    return false;
  }

  flatbuffers::FlatBufferBuilder builder;

  const auto uuid_vec = builder.CreateVector(
      reinterpret_cast<const uint8_t*>(manifest.db_uuid.data()),
      manifest.db_uuid.size());
  const auto wire_schema = builder.CreateString(manifest.wire_schema);
  const auto disk_schema = builder.CreateString(manifest.disk_schema);
  const auto wal_schema = builder.CreateString(manifest.wal_schema);
  const auto hash_algorithm = builder.CreateString(manifest.hash_algorithm);

  const auto manifest_offset = disk::CreateManifest(
      builder, manifest.format_major, manifest.format_minor,
      manifest.page_size, manifest.inline_threshold, uuid_vec, wire_schema,
      disk_schema, wal_schema, hash_algorithm);

  builder.FinishSizePrefixed(manifest_offset, disk::ManifestIdentifier());

  std::ofstream out(manifest_path_, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }

  out.write(reinterpret_cast<const char*>(builder.GetBufferPointer()),
            builder.GetSize());
  out.flush();
  return out.good();
}

}  // namespace jubilant::meta
