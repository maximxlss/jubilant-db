#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace jubilant::meta {

struct TtlCalibration {
  std::uint64_t wall_base{0};
  std::uint64_t mono_base{0};
};

struct SuperBlock {
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
