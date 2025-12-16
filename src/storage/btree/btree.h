#pragma once

#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "storage/pager/pager.h"
#include "storage/vlog/value_log.h"

namespace jubilant::storage::btree {

enum class ValueType {
  kBytes,
  kString,
  kInt64,
  kValueLogRef,
};

struct ValueLogRef {
  vlog::SegmentPointer pointer{};
  std::uint32_t length{0};
  ValueType type{ValueType::kBytes};
};

using Value = std::variant<std::vector<std::byte>, std::string, std::int64_t, ValueLogRef>;

struct RecordMetadata {
  std::uint64_t ttl_epoch_seconds{0};
};

struct Record {
  Value value;
  RecordMetadata metadata;
};

class BTree {
public:
  struct Config {
    Pager* pager{nullptr};
    vlog::ValueLog* value_log{nullptr};
    std::uint32_t inline_threshold{0};
    std::uint64_t root_hint{0};
  };

  explicit BTree(Config config);

  [[nodiscard]] std::optional<Record> Find(const std::string& key) const;
  void Insert(const std::string& key, Record record);
  [[nodiscard]] bool Erase(const std::string& key);
  [[nodiscard]] std::size_t size() const noexcept;

  [[nodiscard]] std::uint64_t root_page_id() const noexcept;

private:
  struct LeafEntry {
    std::string key;
    Record record;
  };

  struct LeafPage {
    std::uint64_t page_id{0};
    std::uint64_t next_leaf{std::numeric_limits<std::uint64_t>::max()};
    std::vector<LeafEntry> entries;
  };

  Pager* pager_;
  vlog::ValueLog* value_log_;
  std::uint32_t inline_threshold_;
  std::uint64_t root_page_id_{0};
  std::map<std::string, Record> in_memory_;
  std::vector<LeafPage> leaf_pages_;

  void LoadFromDisk(std::uint64_t root_hint);
  void Persist();
  void RebuildLeafPages();
  void EnsureRootExists();
  [[nodiscard]] static LeafPage DecodeLeafPage(const Page& page);
  [[nodiscard]] Page EncodeLeafPage(const LeafPage& leaf) const;
  [[nodiscard]] bool ShouldInline(const Record& record) const;
  [[nodiscard]] static std::size_t EncodedEntrySize(const LeafEntry& entry);
  [[nodiscard]] Record Materialize(const LeafEntry& entry) const;
};

} // namespace jubilant::storage::btree
