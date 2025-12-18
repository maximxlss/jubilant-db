#pragma once

#include "storage/storage_common.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace jubilant::storage::wal {

using storage::Lsn;
using storage::SegmentPointer;

enum class RecordType : std::uint8_t {
  kTxnBegin = 0,
  kUpsert = 1,
  kTombstone = 2,
  kTxnCommit = 3,
  kTxnAbort = 4,
  kCheckpoint = 5,
};

struct UpsertPayload {
  std::string key;
  std::vector<std::byte> value;
  // External value pointer when the payload exceeds manifest.inline_threshold. The pointer layout
  // matches storage::SegmentPointer {segment_id, offset, length}.
  std::optional<SegmentPointer> value_ptr;
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
