#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "storage/wal/wal_record.h"

namespace jubilant::storage::checkpoint {

struct CheckpointSnapshot {
  wal::Lsn lsn{0};
  std::uint64_t pages_flushed{0};
};

class Checkpointer {
 public:
  using FlushCallback = std::function<void(wal::Lsn)>;

  Checkpointer() = default;

  void RequestCheckpoint(wal::Lsn target_lsn);
  [[nodiscard]] std::optional<CheckpointSnapshot> RunOnce(
      const FlushCallback& flush);

 private:
  std::optional<wal::Lsn> target_lsn_;
};

}  // namespace jubilant::storage::checkpoint
