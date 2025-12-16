#include "storage/simple_store.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace jubilant::storage {

namespace {

std::string GenerateUuidLikeString() {
  std::mt19937_64 rng(std::random_device{}());
  std::uniform_int_distribution<std::uint64_t> dist;

  auto to_hex = [](std::uint64_t value) {
    std::string out(16, '0');
    static constexpr char kHex[] = "0123456789abcdef";
    for (int i = 15; i >= 0; --i) {
      out[i] = kHex[value & 0xF];
      value >>= 4U;
    }
    return out;
  };

  return to_hex(dist(rng)) + to_hex(dist(rng));
}

} // namespace

SimpleStore::SimpleStore(std::filesystem::path db_dir, meta::ManifestRecord manifest,
                         meta::SuperBlock superblock, Pager pager)
    : db_dir_(std::move(db_dir)), manifest_store_(db_dir_), superblock_store_(db_dir_),
      manifest_(std::move(manifest)), superblock_(superblock), pager_(std::move(pager)) {}

SimpleStore SimpleStore::Open(const std::filesystem::path& db_dir) {
  std::filesystem::create_directories(db_dir);

  meta::ManifestStore manifest_store(db_dir);
  auto manifest = manifest_store.Load();
  if (!manifest.has_value()) {
    manifest = meta::ManifestStore::NewDefault(GenerateUuidLikeString());
    if (!manifest_store.Persist(*manifest)) {
      throw std::runtime_error("Failed to persist manifest");
    }
  }

  meta::SuperBlockStore superblock_store(db_dir);
  auto superblock = superblock_store.LoadActive().value_or(meta::SuperBlock{});

  Pager pager = Pager::Open(db_dir / "data.pages", manifest->page_size);

  SimpleStore store(db_dir, *manifest, superblock, std::move(pager));
  store.LoadFromPages();
  store.superblock_store_.WriteNext(store.superblock_);
  return store;
}

void SimpleStore::LoadFromPages() {
  for (std::uint64_t page_id = 0; page_id < pager_.page_count(); ++page_id) {
    const auto page = pager_.Read(page_id);
    if (!page.has_value() || page->payload.empty()) {
      continue;
    }
    bool tombstone = false;
    const auto decoded = DecodeRecord(page->payload, tombstone);
    if (!decoded.has_value()) {
      continue;
    }

    if (tombstone) {
      (void)tree_.Erase(decoded->first);
    } else {
      tree_.Insert(decoded->first, decoded->second);
    }
    superblock_.root_page_id = page_id;
  }
}

std::optional<btree::Record> SimpleStore::Get(const std::string& key) const {
  return tree_.Find(key);
}

void SimpleStore::AppendRecordPage(const std::string& key, const btree::Record& record,
                                   bool tombstone) {
  const auto payload = EncodeRecord(key, record, tombstone);

  Page page{};
  page.id = pager_.Allocate(PageType::kLeaf);
  page.type = PageType::kLeaf;
  page.payload = payload;
  pager_.Write(page);

  superblock_.root_page_id = page.id;
}

void SimpleStore::Set(const std::string& key, btree::Record record) {
  if (key.empty()) {
    throw std::invalid_argument("Key must not be empty");
  }

  AppendRecordPage(key, record, false);
  tree_.Insert(key, std::move(record));
}

bool SimpleStore::Delete(const std::string& key) {
  if (key.empty()) {
    throw std::invalid_argument("Key must not be empty");
  }

  const bool existed = tree_.Erase(key);
  if (!existed) {
    return false;
  }

  btree::Record tombstone_record{};
  tombstone_record.metadata.ttl_epoch_seconds = 0;
  AppendRecordPage(key, tombstone_record, true);
  return existed;
}

void SimpleStore::Sync() {
  pager_.Sync();
  superblock_store_.WriteNext(superblock_);
  manifest_store_.Persist(manifest_);
}

std::uint64_t SimpleStore::size() const noexcept {
  return tree_.size();
}

std::vector<std::byte> SimpleStore::EncodeRecord(const std::string& key,
                                                 const btree::Record& record, bool tombstone) {
  const auto payload_size = pager_.payload_size();
  std::vector<std::byte> payload(payload_size, std::byte{0});

  const auto key_len = static_cast<std::uint32_t>(key.size());
  const auto ttl = record.metadata.ttl_epoch_seconds;

  std::size_t offset = 0;
  auto write_u8 = [&](std::uint8_t value) { payload[offset++] = static_cast<std::byte>(value); };
  auto write_u32 = [&](std::uint32_t value) {
    if (offset + sizeof(std::uint32_t) > payload.size()) {
      throw std::runtime_error("Record does not fit in page payload");
    }
    std::memcpy(payload.data() + offset, &value, sizeof(std::uint32_t));
    offset += sizeof(std::uint32_t);
  };
  auto write_u64 = [&](std::uint64_t value) {
    if (offset + sizeof(std::uint64_t) > payload.size()) {
      throw std::runtime_error("Record does not fit in page payload");
    }
    std::memcpy(payload.data() + offset, &value, sizeof(std::uint64_t));
    offset += sizeof(std::uint64_t);
  };

  write_u8(tombstone ? 1U : 0U);

  const auto visitor = [&](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, std::vector<std::byte>>) {
      write_u8(static_cast<std::uint8_t>(btree::ValueType::kBytes));
      write_u64(ttl);
      write_u32(key_len);
      write_u32(static_cast<std::uint32_t>(value.size()));
      if (offset + key.size() + value.size() > payload.size()) {
        throw std::runtime_error("Record does not fit in page payload");
      }
      std::memcpy(payload.data() + offset, key.data(), key.size());
      offset += key.size();
      std::memcpy(payload.data() + offset, value.data(), value.size());
      offset += value.size();
    } else if constexpr (std::is_same_v<T, std::string>) {
      write_u8(static_cast<std::uint8_t>(btree::ValueType::kString));
      write_u64(ttl);
      write_u32(key_len);
      write_u32(static_cast<std::uint32_t>(value.size()));
      if (offset + key.size() + value.size() > payload.size()) {
        throw std::runtime_error("Record does not fit in page payload");
      }
      std::memcpy(payload.data() + offset, key.data(), key.size());
      offset += key.size();
      std::memcpy(payload.data() + offset, value.data(), value.size());
      offset += value.size();
    } else {
      write_u8(static_cast<std::uint8_t>(btree::ValueType::kInt64));
      write_u64(ttl);
      write_u32(key_len);
      write_u32(sizeof(std::int64_t));
      if (offset + key.size() + sizeof(std::int64_t) > payload.size()) {
        throw std::runtime_error("Record does not fit in page payload");
      }
      std::memcpy(payload.data() + offset, key.data(), key.size());
      offset += key.size();
      std::memcpy(payload.data() + offset, &value, sizeof(std::int64_t));
      offset += sizeof(std::int64_t);
    }
  };

  std::visit(visitor, record.value);
  return payload;
}

std::optional<std::pair<std::string, btree::Record>>
SimpleStore::DecodeRecord(const std::vector<std::byte>& payload, bool& tombstone) {
  if (payload.size() < sizeof(std::uint8_t) + sizeof(std::uint8_t) + sizeof(std::uint64_t) +
                           2 * sizeof(std::uint32_t)) {
    return std::nullopt;
  }

  std::size_t offset = 0;
  auto read_u8 = [&](std::uint8_t& out) { out = static_cast<std::uint8_t>(payload[offset++]); };
  auto read_u32 = [&](std::uint32_t& out) {
    std::memcpy(&out, payload.data() + offset, sizeof(std::uint32_t));
    offset += sizeof(std::uint32_t);
  };
  auto read_u64 = [&](std::uint64_t& out) {
    std::memcpy(&out, payload.data() + offset, sizeof(std::uint64_t));
    offset += sizeof(std::uint64_t);
  };

  std::uint8_t tombstone_flag = 0;
  read_u8(tombstone_flag);
  tombstone = tombstone_flag != 0;

  std::uint8_t type_byte = 0;
  read_u8(type_byte);

  std::uint64_t ttl{};
  read_u64(ttl);

  std::uint32_t key_len{};
  std::uint32_t value_len{};
  read_u32(key_len);
  read_u32(value_len);

  if (offset + key_len + value_len > payload.size()) {
    return std::nullopt;
  }

  std::string key(reinterpret_cast<const char*>(payload.data() + offset), key_len);
  offset += key_len;

  btree::Record record{};
  record.metadata.ttl_epoch_seconds = ttl;

  switch (static_cast<btree::ValueType>(type_byte)) {
  case btree::ValueType::kBytes: {
    std::vector<std::byte> bytes(value_len);
    std::memcpy(bytes.data(), payload.data() + offset, value_len);
    record.value = std::move(bytes);
    break;
  }
  case btree::ValueType::kString: {
    std::string value(reinterpret_cast<const char*>(payload.data() + offset), value_len);
    record.value = std::move(value);
    break;
  }
  case btree::ValueType::kInt64: {
    std::int64_t value{};
    std::memcpy(&value, payload.data() + offset, sizeof(std::int64_t));
    record.value = value;
    break;
  }
  default:
    return std::nullopt;
  }

  return std::make_pair(std::move(key), std::move(record));
}

} // namespace jubilant::storage
