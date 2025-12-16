#include "storage/wal/wal_manager.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>

using jubilant::storage::wal::RecordType;
using jubilant::storage::wal::WalManager;
using jubilant::storage::wal::WalRecord;

namespace fs = std::filesystem;

namespace {

fs::path TempDir(const std::string& name) {
  const auto dir = fs::temp_directory_path() / name;
  fs::remove_all(dir);
  return dir;
}

} // namespace

TEST(WalManagerTest, AssignsMonotonicLsnsAndReplaysFromDisk) {
  const auto dir = TempDir("jubilant-wal");
  WalManager wal{dir};

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

  EXPECT_EQ(lsn1, 1U);
  EXPECT_EQ(lsn2, 2U);
  EXPECT_EQ(wal.next_lsn(), 3U);

  wal.Flush();

  WalManager wal_reopen{dir};

  const auto replay = wal_reopen.Replay();
  EXPECT_EQ(replay.last_replayed, 2U);
  ASSERT_EQ(replay.committed.size(), 2U);
  EXPECT_EQ(replay.committed.front().type, RecordType::kTxnBegin);
  EXPECT_EQ(replay.committed.front().lsn, 1U);
  EXPECT_EQ(replay.committed.back().type, RecordType::kUpsert);
  EXPECT_EQ(replay.committed.back().lsn, 2U);
}
