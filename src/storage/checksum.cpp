#include "storage/checksum.h"

#include <array>
#include <cstddef>

namespace jubilant::storage {

namespace {

constexpr std::uint32_t kCrc32Polynomial = 0xEDB88320U;
constexpr std::uint32_t kCrc32InitialValue = 0xFFFFFFFFU;
constexpr std::size_t kCrcTableSize = 256;
constexpr int kCrcIterations = 8;
constexpr unsigned kByteShift = 8U;
constexpr std::uint32_t kByteMask = 0xFFU;

using CrcTable = std::array<std::uint32_t, kCrcTableSize>;

constexpr auto BuildTable() -> CrcTable {
  CrcTable table{};
  for (std::size_t i = 0; i < table.size(); ++i) {
    auto crc = static_cast<std::uint32_t>(i);
    for (int bit = 0; bit < kCrcIterations; ++bit) {
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

constexpr CrcTable kCrcTable = BuildTable();

}  // namespace

auto ComputeCrc32(std::span<const std::byte> data) -> std::uint32_t {
  std::uint32_t crc = kCrc32InitialValue;

  for (const auto byte : data) {
    const auto index = static_cast<std::uint8_t>(byte) ^ (crc & kByteMask);
    crc = (crc >> kByteShift) ^ kCrcTable[index];
  }

  return crc ^ kCrc32InitialValue;
}

}  // namespace jubilant::storage
