#include "storage/checksum.h"

#include "storage/storage_common.h"

#include <array>
#include <cstddef>

namespace jubilant::storage {

namespace {

constexpr std::array<std::uint32_t, 256> BuildTable() {
  std::array<std::uint32_t, 256> table{};
  for (std::size_t i = 0; i < table.size(); ++i) {
    auto crc = static_cast<std::uint32_t>(i);
    for (int bit = 0; bit < 8; ++bit) {
      if ((crc & 1U) != 0U) {
        crc = (crc >> 1U) ^ kCrc32Polynomial;
      } else {
        crc >>= 1U;
      }
    }
    table[i] = crc;
  }
  return table;
}

constexpr std::array<std::uint32_t, 256> kCrcTable = BuildTable();

} // namespace

std::uint32_t ComputeCrc32(std::span<const std::byte> data) {
  std::uint32_t crc = kCrc32Seed;

  for (const auto byte : data) {
    const auto index = static_cast<std::uint8_t>(byte) ^ (crc & 0xFFU);
    crc = (crc >> 8U) ^ kCrcTable[index];
  }

  return crc ^ kCrc32FinalXor;
}

} // namespace jubilant::storage
