#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "storage/wal/wal_manager.h"

using jubilant::storage::wal::RecordType;
using jubilant::storage::wal::WalManager;
using jubilant::storage::wal::WalRecord;

TEST(WalManagerTest, AssignsMonotonicLsnsAndReplaysBufferedRecords) {
  WalManager wal{"/tmp/wal"};

  WalRecord begin{};
  begin.type = RecordType::kTxnBegin;
  begin.txn_id = 1;

  WalRecord upsert{};
  upsert.type = RecordType::kUpsert;
  upsert.txn_id = 1;
  upsert.upsert = jubilant::storage::wal::UpsertPayload{
      .key = "key", .value = {std::byte{0x01}}, .value_ptr = std::nullopt};

  const auto lsn1 = wal.Append(begin);
  const auto lsn2 = wal.Append(upsert);

  EXPECT_EQ(lsn1, 1u);
  EXPECT_EQ(lsn2, 2u);
  EXPECT_EQ(wal.next_lsn(), 3u);

  wal.Flush();

  const auto replay = wal.Replay();
  EXPECT_EQ(replay.last_replayed, 2u);
  ASSERT_EQ(replay.committed.size(), 2u);
  EXPECT_EQ(replay.committed.front().type, RecordType::kTxnBegin);
  EXPECT_EQ(replay.committed.back().type, RecordType::kUpsert);
}
