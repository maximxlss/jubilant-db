#include "storage/vlog/value_log.h"

#include "storage/checksum.h"

#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>

namespace jubilant::storage::vlog {

namespace {

struct RecordHeader {
  std::uint32_t length{0};
  std::uint32_t crc{0};
};

} // namespace

ValueLog::ValueLog(std::filesystem::path base_dir) : base_dir_(std::move(base_dir)) {
  std::filesystem::create_directories(base_dir_);
  const auto segment_path = SegmentPath(0);
  if (std::filesystem::exists(segment_path)) {
    next_pointer_.segment_id = 0;
    next_pointer_.offset = std::filesystem::file_size(segment_path);
  }
}

AppendResult ValueLog::Append(const std::vector<std::byte>& data) {
  const auto segment_path = SegmentPath(next_pointer_.segment_id);
  std::ofstream out(segment_path, std::ios::binary | std::ios::app);
  if (!out) {
    throw std::runtime_error("Failed to open value log segment for append");
  }

  RecordHeader header{};
  header.length = static_cast<std::uint32_t>(data.size());
  header.crc = ComputeCrc32(std::span<const std::byte>(data.data(), data.size()));

  AppendResult result{};
  result.pointer = next_pointer_;
  result.length = data.size();

  out.write(reinterpret_cast<const char*>(&header), sizeof(header));
  out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  out.flush();
  if (!out.good()) {
    throw std::runtime_error("Failed to append to value log segment");
  }

  next_pointer_.offset += sizeof(header) + data.size();
  return result;
}

std::optional<std::vector<std::byte>> ValueLog::Read(const SegmentPointer& pointer) const {
  const auto segment_path = SegmentPath(pointer.segment_id);
  if (!std::filesystem::exists(segment_path)) {
    return std::nullopt;
  }

  std::ifstream input_stream(segment_path, std::ios::binary);
  if (!input_stream) {
    return std::nullopt;
  }

  input_stream.seekg(static_cast<std::streamoff>(pointer.offset));
  if (!input_stream.good()) {
    return std::nullopt;
  }

  RecordHeader header{};
  input_stream.read(reinterpret_cast<char*>(&header), sizeof(header));
  if (!input_stream.good()) {
    return std::nullopt;
  }

  std::vector<std::byte> data(header.length);
  input_stream.read(reinterpret_cast<char*>(data.data()),
                    static_cast<std::streamsize>(header.length));
  if (!input_stream.good()) {
    return std::nullopt;
  }

  const auto crc = ComputeCrc32(std::span<const std::byte>(data.data(), data.size()));
  if (crc != header.crc) {
    return std::nullopt;
  }

  return data;
}

void ValueLog::RunGcCycle() {
  // GC scheduling and live-data computation depend on WAL checkpoints. This
  // placeholder keeps the API shape stable until those pieces land.
}

std::filesystem::path ValueLog::SegmentPath(std::uint32_t segment_id) const {
  return base_dir_ / ("segment-" + std::to_string(segment_id) + ".vlog");
}

} // namespace jubilant::storage::vlog
