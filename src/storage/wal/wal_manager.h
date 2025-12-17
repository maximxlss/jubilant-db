#pragma once

#include "storage/wal/wal_record.h"
#include "wal_generated.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

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
  [[nodiscard]] static std::uint32_t ComputeRecordCrc(const WalRecord& record);
  [[nodiscard]] static WalRecord FromFlatBuffer(const ::jubilant::wal::WalRecord& fb_record);
  [[nodiscard]] static std::optional<WalRecord> ReadNext(std::ifstream& stream);
  bool PersistRecord(const WalRecord& record);

  std::filesystem::path wal_dir_;
  std::filesystem::path wal_path_;
  Lsn next_lsn_{1};
  std::vector<WalRecord> buffered_records_;
};

} // namespace jubilant::storage::wal
