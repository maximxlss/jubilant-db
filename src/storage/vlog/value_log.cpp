#include "storage/vlog/value_log.h"

#include <utility>

namespace jubilant::storage::vlog {

ValueLog::ValueLog(std::filesystem::path base_dir) : base_dir_(std::move(base_dir)) {}

auto ValueLog::Append(const std::vector<std::byte>& data) -> AppendResult {
  AppendResult result{};
  result.pointer = next_pointer_;
  result.length = data.size();

  // The next pointer increments monotonically; GC and segment rollover will be
  // added later when durability is wired up.
  next_pointer_.offset += data.size();
  return result;
}

auto ValueLog::Read(const SegmentPointer& /*pointer*/) -> std::optional<std::vector<std::byte>> {
  // Value log persistence is intentionally deferred. Returning nullopt allows
  // callers to handle missing segments gracefully in early tests.
  return std::nullopt;
}

void ValueLog::RunGcCycle() {
  // GC scheduling and live-data computation depend on WAL checkpoints. This
  // placeholder keeps the API shape stable until those pieces land.
}

}  // namespace jubilant::storage::vlog
