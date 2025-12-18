#pragma once

#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>

namespace jubilant::storage {

using PageId = std::uint64_t;
using Lsn = std::uint64_t;
using SegmentId = std::uint32_t;

inline constexpr std::uint32_t kDefaultPageSize = 4096;

// CRC constants shared by pager, WAL, and value log records. The seed/final XOR
// values keep the checksum compatible with the CRC32 used for pages and log
// entries today.
inline constexpr std::uint32_t kCrc32Polynomial = 0xEDB88320U;
inline constexpr std::uint32_t kCrc32Seed = 0xFFFFFFFFU;
inline constexpr std::uint32_t kCrc32FinalXor = 0xFFFFFFFFU;

// Disk pages use this type to differentiate leaves, internal nodes, and
// metadata blocks. Keep the values stable to avoid invalidating existing page
// headers and manifests.
enum class PageType : std::uint8_t {
  kUnknown = 0,
  kLeaf = 1,
  kInternal = 2,
  kManifest = 3,
};

// Common pointer layout for value-log backed payloads. The manifest persists
// the inline-threshold (bytes) so B+Tree, WAL, and value log all agree when to
// emit a pointer instead of an inline value. The pointer schema is shared
// across modules to keep replay and GC logic consistent.
struct SegmentPointer {
  SegmentId segment_id{0};
  std::uint64_t offset{0};
  std::uint64_t length{0};
};

[[nodiscard]] inline std::string FormatSegmentSequence(SegmentId segment_id) {
  std::ostringstream stream;
  stream << std::setfill('0') << std::setw(6) << (segment_id + 1);
  return stream.str();
}

[[nodiscard]] inline std::string WalSegmentName(SegmentId segment_id) {
  return "wal-" + FormatSegmentSequence(segment_id) + ".log";
}

[[nodiscard]] inline std::string ValueLogSegmentName(SegmentId segment_id) {
  return "vlog-" + FormatSegmentSequence(segment_id) + ".seg";
}

[[nodiscard]] inline std::filesystem::path WalSegmentPath(const std::filesystem::path& base_dir,
                                                          SegmentId segment_id) {
  return base_dir / WalSegmentName(segment_id);
}

[[nodiscard]] inline std::filesystem::path
ValueLogSegmentPath(const std::filesystem::path& base_dir, SegmentId segment_id) {
  return base_dir / ValueLogSegmentName(segment_id);
}

} // namespace jubilant::storage
