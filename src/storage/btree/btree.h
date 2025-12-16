#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace jubilant::storage::btree {

enum class ValueType {
  kBytes,
  kString,
  kInt64,
};

using Value = std::variant<std::vector<std::byte>, std::string, std::int64_t>;

struct RecordMetadata {
  std::uint64_t ttl_epoch_seconds{0};
};

struct Record {
  Value value;
  RecordMetadata metadata;
};

class BTree {
public:
  BTree() = default;

  [[nodiscard]] std::optional<Record> Find(const std::string& key) const;
  void Insert(const std::string& key, Record record);
  [[nodiscard]] bool Erase(const std::string& key);
  [[nodiscard]] std::size_t size() const noexcept;

private:
  // A durable, page-backed layout will replace this in-memory map. Keeping the
  // shape close to the desired API lets tests drive out disk persistence later.
  std::map<std::string, Record> in_memory_;
};

} // namespace jubilant::storage::btree
