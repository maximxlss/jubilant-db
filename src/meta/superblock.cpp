#include "meta/superblock.h"

#include <filesystem>
#include <fstream>
#include <span>
#include <utility>

#include "storage/checksum.h"

namespace jubilant::meta {

SuperBlockStore::SuperBlockStore(const std::filesystem::path& base_dir)
    : path_a_(base_dir / "SUPERBLOCK_A"), path_b_(path_a_.parent_path() / "SUPERBLOCK_B") {}

auto SuperBlockStore::LoadActive() const -> std::optional<SuperBlock> {
  const auto read_block = [](const std::filesystem::path& path) -> std::optional<SuperBlock> {
    if (!std::filesystem::exists(path)) {
      return std::nullopt;
    }

    std::ifstream input_stream(path, std::ios::binary);
    if (!input_stream) {
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
    input_stream.read(reinterpret_cast<char*>(&persisted),
                      static_cast<std::streamsize>(sizeof(Persisted)));
    if (!input_stream) {
      return std::nullopt;
    }

    const auto payload_span = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(&persisted), sizeof(Persisted) - sizeof(std::uint64_t));
    const auto crc = storage::ComputeCrc32(payload_span);
    if (crc != persisted.crc) {
      return std::nullopt;
    }

    SuperBlock superblock{};
    superblock.generation = persisted.generation;
    superblock.root_page_id = persisted.root_page_id;
    superblock.last_checkpoint_lsn = persisted.last_checkpoint_lsn;
    superblock.ttl_calibration.wall_base = persisted.wall_base;
    superblock.ttl_calibration.mono_base = persisted.mono_base;
    return superblock;
  };

  const auto primary_block = read_block(path_a_);
  const auto secondary_block = read_block(path_b_);

  if (primary_block && secondary_block) {
    return primary_block->generation >= secondary_block->generation ? primary_block
                                                                    : secondary_block;
  }
  if (primary_block) {
    return primary_block;
  }
  if (secondary_block) {
    return secondary_block;
  }
  return std::nullopt;
}

auto SuperBlockStore::WriteNext(const SuperBlock& superblock) -> bool {
  const auto current = LoadActive();
  const std::uint64_t next_generation = current.has_value() ? current->generation + 1 : 1;

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
      reinterpret_cast<const std::byte*>(&persisted), sizeof(Persisted) - sizeof(std::uint64_t));
  persisted.crc = storage::ComputeCrc32(payload_span);

  out.write(reinterpret_cast<const char*>(&persisted), sizeof(Persisted));
  out.flush();
  return out.good();
}

}  // namespace jubilant::meta
