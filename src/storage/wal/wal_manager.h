#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include "storage/wal/wal_record.h"

namespace jubilant::storage::wal {

struct ReplayResult {
  Lsn last_replayed{0};
  std::vector<WalRecord> committed;
};

class WalManager {
 public:
  explicit WalManager(std::filesystem::path base_dir);

  [[nodiscard]] Lsn Append(const WalRecord& record);
  void Flush();
  [[nodiscard]] ReplayResult Replay() const;
  [[nodiscard]] Lsn next_lsn() const noexcept;

 private:
  std::filesystem::path wal_dir_;
  Lsn next_lsn_{1};
  std::vector<WalRecord> buffered_records_;
};

}  // namespace jubilant::storage::wal
