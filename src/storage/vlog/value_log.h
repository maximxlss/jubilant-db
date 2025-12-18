#pragma once

#include "storage/storage_common.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace jubilant::storage::vlog {

using storage::SegmentPointer;

struct AppendResult {
  // Full segment pointer (segment_id, offset, length) for the appended payload.
  SegmentPointer pointer{};
};

class ValueLog {
public:
  explicit ValueLog(std::filesystem::path base_dir);

  [[nodiscard]] AppendResult Append(const std::vector<std::byte>& data);
  [[nodiscard]] std::optional<std::vector<std::byte>> Read(const SegmentPointer& pointer) const;
  void RunGcCycle();

private:
  std::filesystem::path base_dir_;
  SegmentPointer next_pointer_{};

  [[nodiscard]] std::filesystem::path SegmentPath(SegmentId segment_id) const;
};

} // namespace jubilant::storage::vlog
