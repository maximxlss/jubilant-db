#include "meta/superblock.h"

#include <filesystem>
#include <fstream>
#include <span>
#include <utility>

#include "storage/checksum.h"

namespace jubilant::meta {

SuperBlockStore::SuperBlockStore(std::filesystem::path base_dir)
    : path_a_(std::move(base_dir) / "SUPERBLOCK_A"),
      path_b_(path_a_.parent_path() / "SUPERBLOCK_B") {}

std::optional<SuperBlock> SuperBlockStore::LoadActive() const {
  const auto read_block = [](const std::filesystem::path& path)
      -> std::optional<SuperBlock> {
    if (!std::filesystem::exists(path)) {
      return std::nullopt;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
      return std::nullopt;
    }

    struct Persisted {
      std::uint64_t generation;
      std::uint64_t root_page_id;
      std::uint64_t last_checkpoint_lsn;
      std::uint64_t wall_base;
      std::uint64_t mono_base;
      std::uint64_t crc;
    };

    Persisted persisted{};
    in.read(reinterpret_cast<char*>(&persisted), sizeof(Persisted));
    if (!in) {
      return std::nullopt;
    }

    const auto payload_span = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(&persisted),
        sizeof(Persisted) - sizeof(std::uint64_t));
    const auto crc = storage::ComputeCrc32(payload_span);
    if (crc != persisted.crc) {
      return std::nullopt;
    }

    SuperBlock sb{};
    sb.generation = persisted.generation;
    sb.root_page_id = persisted.root_page_id;
    sb.last_checkpoint_lsn = persisted.last_checkpoint_lsn;
    sb.ttl_calibration.wall_base = persisted.wall_base;
    sb.ttl_calibration.mono_base = persisted.mono_base;
    return sb;
  };

  const auto a = read_block(path_a_);
  const auto b = read_block(path_b_);

  if (a && b) {
    return a->generation >= b->generation ? a : b;
  }
  if (a) {
    return a;
  }
  if (b) {
    return b;
  }
  return std::nullopt;
}

bool SuperBlockStore::WriteNext(const SuperBlock& superblock) {
  const auto current = LoadActive();
  const std::uint64_t next_generation =
      current.has_value() ? current->generation + 1 : 1;

  const bool write_to_a = (next_generation % 2) == 1;
  const auto& target = write_to_a ? path_a_ : path_b_;

  std::ofstream out(target, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }

  struct Persisted {
    std::uint64_t generation;
    std::uint64_t root_page_id;
    std::uint64_t last_checkpoint_lsn;
    std::uint64_t wall_base;
    std::uint64_t mono_base;
    std::uint64_t crc;
  };

  Persisted persisted{};
  persisted.generation = next_generation;
  persisted.root_page_id = superblock.root_page_id;
  persisted.last_checkpoint_lsn = superblock.last_checkpoint_lsn;
  persisted.wall_base = superblock.ttl_calibration.wall_base;
  persisted.mono_base = superblock.ttl_calibration.mono_base;

  const auto payload_span = std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(&persisted),
      sizeof(Persisted) - sizeof(std::uint64_t));
  persisted.crc = storage::ComputeCrc32(payload_span);

  out.write(reinterpret_cast<const char*>(&persisted), sizeof(Persisted));
  out.flush();
  return out.good();
}

}  // namespace jubilant::meta
