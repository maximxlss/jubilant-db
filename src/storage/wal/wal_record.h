#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace jubilant::storage::wal {

using Lsn = std::uint64_t;

enum class RecordType : std::uint8_t {
  kTxnBegin = 0,
  kUpsert = 1,
  kTombstone = 2,
  kTxnCommit = 3,
  kTxnAbort = 4,
  kCheckpoint = 5,
};

struct ValuePointer {
  std::uint32_t segment_id{0};
  std::uint64_t offset{0};
  std::uint64_t length{0};
};

struct UpsertPayload {
  std::string key;
  std::vector<std::byte> value;
  std::optional<ValuePointer> value_ptr;
  std::uint64_t ttl_epoch_seconds{0};
};

struct WalRecord {
  RecordType type{RecordType::kTxnBegin};
  std::uint64_t txn_id{0};
  std::optional<UpsertPayload> upsert;
  std::optional<std::string> tombstone_key;
  Lsn lsn{0};
};

} // namespace jubilant::storage::wal
