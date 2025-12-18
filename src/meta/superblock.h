#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace jubilant::meta {

struct TtlCalibration {
  // wall_base stores the Unix epoch seconds captured alongside the monotonic base time in
  // mono_base. Both values are recorded together at startup to translate steady_clock deltas back
  // into a stable wall-clock domain for TTL evaluation.
  std::uint64_t wall_base{0};
  std::uint64_t mono_base{0};
};

struct SuperBlock {
  // Generation covers the active root page id and last checkpoint LSN for a pager configured with
  // the manifest's page_size/inline_threshold. The inline policy lives in the manifest, but the
  // superblock assumes it remains stable so pointers and page ids stay valid across restarts.
  std::uint64_t generation{0};
  std::uint64_t root_page_id{0};
  std::uint64_t last_checkpoint_lsn{0};
  TtlCalibration ttl_calibration{};
};

class SuperBlockStore {
public:
  explicit SuperBlockStore(const std::filesystem::path& base_dir);

  [[nodiscard]] std::optional<SuperBlock> LoadActive() const;
  bool WriteNext(const SuperBlock& superblock);

private:
  std::filesystem::path path_a_;
  std::filesystem::path path_b_;
};

} // namespace jubilant::meta
