#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace jubilant::storage::vlog {

struct SegmentPointer {
  std::uint32_t segment_id{0};
  std::uint64_t offset{0};
};

struct AppendResult {
  SegmentPointer pointer{};
  std::uint64_t length{0};
};

class ValueLog {
public:
  explicit ValueLog(std::filesystem::path base_dir);

  [[nodiscard]] AppendResult Append(const std::vector<std::byte>& data);
  [[nodiscard]] static std::optional<std::vector<std::byte>> Read(const SegmentPointer& pointer);
  void RunGcCycle();

private:
  std::filesystem::path base_dir_;
  SegmentPointer next_pointer_{};
};

} // namespace jubilant::storage::vlog
