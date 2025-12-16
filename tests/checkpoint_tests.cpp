#include <optional>

#include <gtest/gtest.h>

#include "storage/checkpoint/checkpointer.h"

using jubilant::storage::checkpoint::Checkpointer;
using jubilant::storage::checkpoint::CheckpointSnapshot;
using jubilant::storage::wal::Lsn;

TEST(CheckpointerTest, SkipsWhenNoCheckpointRequested) {
  Checkpointer checkpointer;
  bool flushed = false;

  const auto snapshot =
      checkpointer.RunOnce([&](Lsn) { flushed = true; });

  EXPECT_FALSE(snapshot.has_value());
  EXPECT_FALSE(flushed);
}

TEST(CheckpointerTest, RunsFlushCallbackAndResetsRequest) {
  Checkpointer checkpointer;
  checkpointer.RequestCheckpoint(5);

  bool flushed = false;
  auto snapshot = checkpointer.RunOnce([&](Lsn lsn) {
    flushed = true;
    EXPECT_EQ(lsn, 5u);
  });

  ASSERT_TRUE(snapshot.has_value());
  EXPECT_EQ(snapshot->lsn, 5u);
  EXPECT_EQ(snapshot->pages_flushed, 0u);
  EXPECT_TRUE(flushed);

  flushed = false;
  snapshot = checkpointer.RunOnce([&](Lsn) { flushed = true; });
  EXPECT_FALSE(snapshot.has_value());
  EXPECT_FALSE(flushed);
}

