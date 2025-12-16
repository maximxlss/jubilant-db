#include "storage/vlog/value_log.h"

#include <cstddef>
#include <gtest/gtest.h>
#include <filesystem>
#include <vector>

using jubilant::storage::vlog::AppendResult;
using jubilant::storage::vlog::SegmentPointer;
using jubilant::storage::vlog::ValueLog;

namespace fs = std::filesystem;

namespace {

fs::path TempDir(const std::string& name) {
  const auto dir = fs::temp_directory_path() / name;
  fs::remove_all(dir);
  return dir;
}

} // namespace

TEST(ValueLogTest, AppendsReturnMonotonicPointers) {
  const auto dir = TempDir("value-log-append");
  ValueLog vlog{dir};

  std::vector<std::byte> first(4, std::byte{0x01});
  const auto first_result = vlog.Append(first);

  std::vector<std::byte> second(2, std::byte{0x02});
  const auto second_result = vlog.Append(second);

  EXPECT_EQ(first_result.pointer.segment_id, 0U);
  EXPECT_EQ(first_result.pointer.offset, 0U);
  EXPECT_EQ(first_result.length, first.size());

  EXPECT_EQ(second_result.pointer.segment_id, 0U);
  EXPECT_EQ(second_result.pointer.offset, first.size() + sizeof(std::uint32_t) * 2);
  EXPECT_EQ(second_result.length, second.size());
}

TEST(ValueLogTest, PersistsAndReadsValues) {
  const auto dir = TempDir("value-log-read");
  ValueLog vlog{dir};

  std::vector<std::byte> payload(3, std::byte{0xCC});
  const auto append_result = vlog.Append(payload);

  const auto read_back = vlog.Read(append_result.pointer);
  ASSERT_TRUE(read_back.has_value());
  EXPECT_EQ(read_back->size(), payload.size());
  EXPECT_EQ(read_back->at(0), std::byte{0xCC});
}
