#pragma once

#include "storage/pager/pager.h"
#include "storage/storage_common.h"
#include "storage/ttl/ttl_clock.h"
#include "storage/vlog/value_log.h"

#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace jubilant::storage::btree {

enum class ValueType : std::uint8_t {
  kBytes,
  kString,
  kInt64,
  kValueLogRef,
};

struct ValueLogRef {
  // SegmentPointer layout mirrors WAL/value-log spill records so leaves can be replayed without
  // reinterpretation.
  SegmentPointer pointer{};
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
    // Matches manifest.inline_threshold so inline vs. value-log spill decisions stay stable across
    // WAL replay and checkpoints.
    std::uint32_t inline_threshold{0};
    PageId root_hint{0};
    const ttl::TtlClock* ttl_clock{nullptr};
  };

  explicit BTree(Config config);

  [[nodiscard]] std::optional<Record> Find(const std::string& key) const;
  void Insert(const std::string& key, Record record);
  [[nodiscard]] bool Erase(const std::string& key);
  [[nodiscard]] std::size_t size() const noexcept;

  [[nodiscard]] PageId root_page_id() const noexcept;

private:
  struct LeafEntry {
    std::string key;
    Record record;
  };

  struct LeafPage {
    PageId page_id{0};
    PageId next_leaf{std::numeric_limits<PageId>::max()};
    std::vector<LeafEntry> entries;
  };

  Pager* pager_;
  vlog::ValueLog* value_log_;
  std::uint32_t inline_threshold_;
  PageId root_page_id_{0};
  const ttl::TtlClock* ttl_clock_{nullptr};
  std::map<std::string, Record> in_memory_;
  std::vector<LeafPage> leaf_pages_;

  void LoadFromDisk(PageId root_hint);
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
