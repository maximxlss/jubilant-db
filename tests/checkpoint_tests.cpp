#include "storage/checkpoint/checkpointer.h"

#include <gtest/gtest.h>
#include <optional>

using jubilant::storage::Lsn;
using jubilant::storage::checkpoint::Checkpointer;
using jubilant::storage::checkpoint::CheckpointSnapshot;

TEST(CheckpointerTest, SkipsWhenNoCheckpointRequested) {
  Checkpointer checkpointer;
  bool flushed = false;

  const auto snapshot = checkpointer.RunOnce([&](Lsn) { flushed = true; });

  EXPECT_FALSE(snapshot.has_value());
  EXPECT_FALSE(flushed);
}

TEST(CheckpointerTest, RunsFlushCallbackAndResetsRequest) {
  Checkpointer checkpointer;
  checkpointer.RequestCheckpoint(5);

  bool flushed = false;
  auto snapshot = checkpointer.RunOnce([&](Lsn lsn) {
    flushed = true;
    EXPECT_EQ(lsn, 5U);
  });

  ASSERT_TRUE(snapshot.has_value());
  if (!snapshot.has_value()) {
    return;
  }
  const auto& snapshot_value = snapshot.value();
  EXPECT_EQ(snapshot_value.lsn, 5U);
  EXPECT_EQ(snapshot_value.pages_flushed, 0U);
  EXPECT_TRUE(flushed);

  flushed = false;
  snapshot = checkpointer.RunOnce([&](Lsn) { flushed = true; });
  EXPECT_FALSE(snapshot.has_value());
  EXPECT_FALSE(flushed);
}
