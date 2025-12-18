#include "storage/wal/wal_manager.h"

#include "storage/checksum.h"
#include "storage/storage_common.h"
#include "wal_generated.h"

#include <flatbuffers/verifier.h>
#include <utility>

namespace wal_fb = ::jubilant::wal;

namespace {

std::vector<std::byte> BuildCrcPayload(const jubilant::storage::wal::WalRecord& record) {
  std::vector<std::byte> payload;
  const auto append_bytes = [&payload](const void* data, std::size_t length) {
    const auto* bytes = reinterpret_cast<const std::byte*>(data);
    payload.insert(payload.end(), bytes, bytes + length);
  };

  const auto type_byte = static_cast<std::uint8_t>(record.type);
  append_bytes(&type_byte, sizeof(type_byte));
  append_bytes(&record.lsn, sizeof(record.lsn));
  append_bytes(&record.txn_id, sizeof(record.txn_id));

  if (record.upsert.has_value()) {
    const auto& upsert = record.upsert.value();
    append_bytes(&upsert.ttl_epoch_seconds, sizeof(upsert.ttl_epoch_seconds));

    const auto key_size = static_cast<std::uint32_t>(upsert.key.size());
    append_bytes(&key_size, sizeof(key_size));
    append_bytes(upsert.key.data(), upsert.key.size());

    const auto value_size = static_cast<std::uint32_t>(upsert.value.size());
    append_bytes(&value_size, sizeof(value_size));
    append_bytes(upsert.value.data(), upsert.value.size());
  } else if (record.tombstone_key.has_value()) {
    const auto key_size = static_cast<std::uint32_t>(record.tombstone_key->size());
    append_bytes(&key_size, sizeof(key_size));
    append_bytes(record.tombstone_key->data(), record.tombstone_key->size());
  }

  return payload;
}

} // namespace

namespace jubilant::storage::wal {

WalManager::WalManager(std::filesystem::path base_dir)
    : wal_dir_(std::move(base_dir)), wal_path_(WalSegmentPath(wal_dir_, 0)) {
  std::filesystem::create_directories(wal_dir_);

  const auto replay = Replay();
  buffered_records_ = replay.committed;
  next_lsn_ = replay.last_replayed + 1;
}

Lsn WalManager::Append(const WalRecord& record) {
  WalRecord to_persist = record;
  to_persist.lsn = next_lsn_++;

  buffered_records_.push_back(to_persist);
  PersistRecord(to_persist);

  return to_persist.lsn;
}

void WalManager::Flush() {
  // Persistence happens during append; flush remains as the explicit fsync
  // hook once segment management lands.
}

ReplayResult WalManager::Replay() const {
  ReplayResult result{};

  std::ifstream stream(wal_path_, std::ios::binary);
  if (!stream) {
    return result;
  }

  while (true) {
    auto record = ReadNext(stream);
    if (!record.has_value()) {
      if (!stream) {
        break;
      }
      continue;
    }

    result.last_replayed = record->lsn;
    result.committed.push_back(std::move(*record));
  }

  return result;
}

Lsn WalManager::next_lsn() const noexcept {
  return next_lsn_;
}

std::uint32_t WalManager::ComputeRecordCrc(const WalRecord& record) {
  const auto payload = BuildCrcPayload(record);
  return storage::ComputeCrc32(payload);
}

WalRecord WalManager::FromFlatBuffer(const wal_fb::WalRecord& fb_record) {
  WalRecord record{};
  record.type = static_cast<RecordType>(fb_record.type());
  record.lsn = fb_record.lsn();

  if (const auto* marker = fb_record.marker()) {
    record.txn_id = marker->txn_id();
  } else if (const auto* upsert = fb_record.upsert()) {
    record.txn_id = upsert->txn_id();
  } else if (const auto* tombstone = fb_record.tombstone()) {
    record.txn_id = tombstone->txn_id();
  }

  if (const auto* upsert = fb_record.upsert()) {
    UpsertPayload payload{};
    if (const auto* key = upsert->key()) {
      payload.key.assign(reinterpret_cast<const char*>(key->Data()), key->size());
    }
    if (const auto* value = upsert->inline_value()) {
      payload.value.assign(reinterpret_cast<const std::byte*>(value->Data()),
                           reinterpret_cast<const std::byte*>(value->Data()) + value->size());
    }
    payload.ttl_epoch_seconds = upsert->ttl_epoch_seconds();
    record.upsert = std::move(payload);
  }

  if (const auto* tombstone = fb_record.tombstone()) {
    if (const auto* key = tombstone->key()) {
      record.tombstone_key = std::string(reinterpret_cast<const char*>(key->Data()), key->size());
    }
  }

  return record;
}

std::optional<WalRecord> WalManager::ReadNext(std::ifstream& stream) {
  std::uint32_t size = 0;
  stream.read(reinterpret_cast<char*>(&size), sizeof(size));
  if (!stream) {
    return std::nullopt;
  }

  std::vector<std::byte> buffer(size);
  stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
  if (!stream) {
    return std::nullopt;
  }

  flatbuffers::Verifier verifier(reinterpret_cast<const uint8_t*>(buffer.data()), buffer.size());
  if (!wal_fb::VerifyWalRecordBuffer(verifier)) {
    return std::nullopt;
  }

  const auto* fb_record = wal_fb::GetWalRecord(buffer.data());
  if (fb_record == nullptr) {
    return std::nullopt;
  }

  auto record = FromFlatBuffer(*fb_record);
  const auto expected_crc = ComputeRecordCrc(record);
  if (expected_crc != fb_record->crc()) {
    return std::nullopt;
  }

  return record;
}

bool WalManager::PersistRecord(const WalRecord& record) {
  flatbuffers::FlatBufferBuilder builder;

  flatbuffers::Offset<wal_fb::Upsert> upsert_offset;
  flatbuffers::Offset<wal_fb::Tombstone> tombstone_offset;
  flatbuffers::Offset<wal_fb::TxnMarker> marker_offset;

  switch (record.type) {
  case RecordType::kUpsert: {
    if (record.upsert.has_value()) {
      const auto& upsert = record.upsert.value();
      const auto key_vec = builder.CreateVector(reinterpret_cast<const uint8_t*>(upsert.key.data()),
                                                upsert.key.size());
      const auto value_vec = builder.CreateVector(
          reinterpret_cast<const uint8_t*>(upsert.value.data()), upsert.value.size());
      flatbuffers::Offset<wal_fb::ValuePointer> value_ptr_offset{};
      if (upsert.value_ptr.has_value()) {
        const auto& ptr = upsert.value_ptr.value();
        value_ptr_offset =
            wal_fb::CreateValuePointer(builder, ptr.segment_id, ptr.offset, ptr.length);
      }
      upsert_offset =
          wal_fb::CreateUpsert(builder, record.txn_id, key_vec, wal_fb::ValueKind::Bytes, value_vec,
                               value_ptr_offset, upsert.ttl_epoch_seconds);
    }
    break;
  }
  case RecordType::kTombstone: {
    if (record.tombstone_key.has_value()) {
      const auto key_vec =
          builder.CreateVector(reinterpret_cast<const uint8_t*>(record.tombstone_key->data()),
                               record.tombstone_key->size());
      tombstone_offset = wal_fb::CreateTombstone(builder, record.txn_id, key_vec);
    }
    break;
  }
  case RecordType::kTxnBegin:
  case RecordType::kTxnCommit:
  case RecordType::kTxnAbort:
  case RecordType::kCheckpoint: {
    marker_offset = wal_fb::CreateTxnMarker(builder, record.txn_id);
    break;
  }
  }

  const auto crc = ComputeRecordCrc(record);
  const auto wal_offset =
      wal_fb::CreateWalRecord(builder, static_cast<wal_fb::RecordType>(record.type), record.lsn,
                              upsert_offset, tombstone_offset, marker_offset, crc);
  builder.Finish(wal_offset, wal_fb::WalRecordIdentifier());

  auto* const buffer_pointer = builder.GetBufferPointer();
  const auto buffer_size = builder.GetSize();

  std::ofstream out(wal_path_, std::ios::binary | std::ios::app);
  if (!out) {
    return false;
  }

  const auto size = static_cast<std::uint32_t>(buffer_size);
  out.write(reinterpret_cast<const char*>(&size), sizeof(size));
  out.write(reinterpret_cast<const char*>(buffer_pointer),
            static_cast<std::streamsize>(buffer_size));
  out.flush();
  return out.good();
}

} // namespace jubilant::storage::wal
