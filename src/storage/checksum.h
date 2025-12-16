#pragma once

#include <cstdint>
#include <span>

namespace jubilant::storage {

// Minimal CRC32 implementation used for pages and superblocks. This keeps
// v0.0.1 durability checks lightweight while providing a stable checksum for
// validation.
std::uint32_t ComputeCrc32(std::span<const std::byte> data);

}  // namespace jubilant::storage
