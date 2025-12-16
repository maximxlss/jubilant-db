#include "storage/checkpoint/checkpointer.h"

namespace jubilant::storage::checkpoint {

void Checkpointer::RequestCheckpoint(wal::Lsn target_lsn) {
  target_lsn_ = target_lsn;
}

std::optional<CheckpointSnapshot> Checkpointer::RunOnce(
    const FlushCallback& flush) {
  if (!target_lsn_.has_value()) {
    return std::nullopt;
  }

  // Actual page flushing will use the provided callback to respect the WAL
  // durability ordering. For now we report a no-op snapshot to unblock higher
  // level scheduling and observability wiring.
  flush(*target_lsn_);

  CheckpointSnapshot snapshot{};
  snapshot.lsn = *target_lsn_;
  snapshot.pages_flushed = 0;

  target_lsn_.reset();
  return snapshot;
}

}  // namespace jubilant::storage::checkpoint
