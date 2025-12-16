#include "storage/vlog/value_log.h"

#include <cstddef>
#include <gtest/gtest.h>
#include <vector>

using jubilant::storage::vlog::AppendResult;
using jubilant::storage::vlog::SegmentPointer;
using jubilant::storage::vlog::ValueLog;

TEST(ValueLogTest, AppendsReturnMonotonicPointers) {
  ValueLog vlog{"/tmp/value_log"};

  std::vector<std::byte> first(4, std::byte{0x01});
  const auto first_result = vlog.Append(first);

  std::vector<std::byte> second(2, std::byte{0x02});
  const auto second_result = vlog.Append(second);

  EXPECT_EQ(first_result.pointer.segment_id, 0U);
  EXPECT_EQ(first_result.pointer.offset, 0U);
  EXPECT_EQ(first_result.length, first.size());

  EXPECT_EQ(second_result.pointer.segment_id, 0U);
  EXPECT_EQ(second_result.pointer.offset, first.size());
  EXPECT_EQ(second_result.length, second.size());
}

TEST(ValueLogTest, ReadReturnsNulloptUntilPersistenceExists) {
  ValueLog vlog{"/tmp/value_log"};
  SegmentPointer pointer{};

  EXPECT_FALSE(vlog.Read(pointer).has_value());
}
