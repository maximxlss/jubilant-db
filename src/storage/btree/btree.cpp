#include "storage/btree/btree.h"

namespace jubilant::storage::btree {

std::optional<Record> BTree::Find(const std::string& key) const {
  auto it = in_memory_.find(key);
  if (it == in_memory_.end()) {
    return std::nullopt;
  }
  return it->second;
}

void BTree::Insert(const std::string& key, Record record) {
  in_memory_.insert_or_assign(key, std::move(record));
}

bool BTree::Erase(const std::string& key) { return in_memory_.erase(key) > 0; }

std::size_t BTree::size() const noexcept { return in_memory_.size(); }

}  // namespace jubilant::storage::btree
