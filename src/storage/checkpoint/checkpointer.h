#pragma once

#include "storage/storage_common.h"

#include <cstdint>
#include <functional>
#include <optional>

namespace jubilant::storage::checkpoint {

struct CheckpointSnapshot {
  Lsn lsn{0};
  std::uint64_t pages_flushed{0};
};

class Checkpointer {
public:
  using FlushCallback = std::function<void(Lsn)>;

  Checkpointer() = default;

  void RequestCheckpoint(Lsn target_lsn);
  [[nodiscard]] std::optional<CheckpointSnapshot> RunOnce(const FlushCallback& flush);

private:
  std::optional<Lsn> target_lsn_;
};

} // namespace jubilant::storage::checkpoint
