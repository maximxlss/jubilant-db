#include "storage/wal/wal_manager.h"

#include <utility>

namespace jubilant::storage::wal {

WalManager::WalManager(std::filesystem::path base_dir) : wal_dir_(std::move(base_dir)) {}

auto WalManager::Append(const WalRecord& record) -> Lsn {
  const Lsn assigned = next_lsn_++;
  buffered_records_.push_back(record);
  return assigned;
}

void WalManager::Flush() {
  // The durability pipeline is intentionally deferred. When wiring up IO, this
  // method should fsync and advance durability cursors.
}

auto WalManager::Replay() const -> ReplayResult {
  ReplayResult result{};
  result.last_replayed = next_lsn_ == 0 ? 0 : next_lsn_ - 1;

  // Recovery will eventually read segments from disk and filter by commit
  // markers. Until then, this returns the buffered logical ops for deterministic
  // unit testing.
  result.committed = buffered_records_;
  return result;
}

auto WalManager::next_lsn() const noexcept -> Lsn { return next_lsn_; }

}  // namespace jubilant::storage::wal
