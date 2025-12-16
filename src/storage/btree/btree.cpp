#include "storage/btree/btree.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace jubilant::storage::btree {

namespace {

constexpr std::uint64_t kInvalidPageId = std::numeric_limits<std::uint64_t>::max();

enum class EncodedValueTag : std::uint8_t {
  kInlineBytes = 0,
  kInlineString = 1,
  kInlineInt64 = 2,
  kValueLogBytes = 3,
  kValueLogString = 4,
};

struct LeafHeader {
  std::uint8_t is_leaf{1U};
  std::uint16_t entry_count{0};
  std::uint8_t reserved{0};
  std::uint64_t next_leaf{kInvalidPageId};
};

constexpr std::size_t kEntryHeaderSize =
    sizeof(std::uint16_t) + sizeof(std::uint8_t) + sizeof(std::uint64_t) + sizeof(std::uint32_t);

} // namespace

BTree::BTree(Config config)
    : pager_(config.pager), value_log_(config.value_log),
      inline_threshold_(config.inline_threshold), root_page_id_(config.root_hint) {
  if (pager_ == nullptr) {
    throw std::invalid_argument("Pager must not be null");
  }
  if (inline_threshold_ == 0 || inline_threshold_ >= pager_->payload_size()) {
    throw std::invalid_argument("Inline threshold must be within (0, payload_size)");
  }
  EnsureRootExists();
  LoadFromDisk(root_page_id_);
}

void BTree::EnsureRootExists() {
  if (pager_->page_count() == 0) {
    Page root{};
    root.id = pager_->Allocate(PageType::kLeaf);
    root.type = PageType::kLeaf;
    root.payload.assign(pager_->payload_size(), std::byte{0});
    LeafHeader header{};
    header.next_leaf = kInvalidPageId;
    std::memcpy(root.payload.data(), &header, sizeof(header));
    root_page_id_ = root.id;
    pager_->Write(root);
  } else if (root_page_id_ >= pager_->page_count()) {
    root_page_id_ = 0;
  }
}

void BTree::LoadFromDisk(std::uint64_t root_hint) {
  if (pager_->page_count() == 0) {
    return;
  }

  const auto root_page = pager_->Read(root_hint);
  if (!root_page.has_value()) {
    return;
  }

  if (root_page->type != PageType::kLeaf) {
    throw std::runtime_error("Root page is not a leaf node");
  }

  auto current = *root_page;
  while (true) {
    const auto leaf = DecodeLeafPage(current);
    leaf_pages_.push_back(leaf);
    for (const auto& entry : leaf.entries) {
      in_memory_.insert_or_assign(entry.key, entry.record);
    }

    if (leaf.next_leaf == kInvalidPageId) {
      break;
    }
    const auto next = pager_->Read(leaf.next_leaf);
    if (!next.has_value()) {
      break;
    }
    current = *next;
  }
}

std::optional<Record> BTree::Find(const std::string& key) const {
  const auto iter = in_memory_.find(key);
  if (iter == in_memory_.end()) {
    return std::nullopt;
  }
  return Materialize(LeafEntry{.key = iter->first, .record = iter->second});
}

void BTree::Insert(const std::string& key, Record record) {
  if (key.empty()) {
    throw std::invalid_argument("Key must not be empty");
  }

  if (!ShouldInline(record)) {
    if (std::holds_alternative<ValueLogRef>(record.value)) {
      in_memory_.insert_or_assign(key, std::move(record));
      Persist();
      return;
    }
    if (value_log_ == nullptr) {
      throw std::invalid_argument("Value log required for oversized values");
    }
    const auto serialized = [&]() {
      if (auto bytes = std::get_if<std::vector<std::byte>>(&record.value)) {
        return *bytes;
      }
      if (auto str = std::get_if<std::string>(&record.value)) {
        return std::vector<std::byte>(
            reinterpret_cast<const std::byte*>(str->data()),
            reinterpret_cast<const std::byte*>(str->data() + str->size()));
      }
      throw std::invalid_argument("Unsupported value type for value log");
    }();

    const auto appended = value_log_->Append(serialized);
    ValueLogRef ref{};
    ref.pointer = appended.pointer;
    ref.length = static_cast<std::uint32_t>(appended.length);
    ref.type =
        std::holds_alternative<std::string>(record.value) ? ValueType::kString : ValueType::kBytes;
    record.value = ref;
  }

  in_memory_.insert_or_assign(key, std::move(record));
  Persist();
}

bool BTree::Erase(const std::string& key) {
  const auto erased = in_memory_.erase(key) > 0;
  if (erased) {
    Persist();
  }
  return erased;
}

std::size_t BTree::size() const noexcept {
  return in_memory_.size();
}

std::uint64_t BTree::root_page_id() const noexcept {
  return root_page_id_;
}

bool BTree::ShouldInline(const Record& record) const {
  if (std::holds_alternative<std::int64_t>(record.value)) {
    return true;
  }
  if (const auto* bytes = std::get_if<std::vector<std::byte>>(&record.value)) {
    return bytes->size() <= inline_threshold_;
  }
  if (const auto* str = std::get_if<std::string>(&record.value)) {
    return str->size() <= inline_threshold_;
  }
  if (std::holds_alternative<ValueLogRef>(record.value)) {
    return false;
  }
  return true;
}

Page BTree::EncodeLeafPage(const LeafPage& leaf) const {
  Page page{};
  page.id = leaf.page_id;
  page.type = PageType::kLeaf;
  page.payload.assign(pager_->payload_size(), std::byte{0});

  LeafHeader header{};
  if (leaf.entries.size() > std::numeric_limits<std::uint16_t>::max()) {
    throw std::runtime_error("Leaf contains too many entries");
  }
  header.entry_count = static_cast<std::uint16_t>(leaf.entries.size());
  header.next_leaf = leaf.next_leaf;

  std::size_t offset = 0;
  std::memcpy(page.payload.data() + offset, &header, sizeof(LeafHeader));
  offset += sizeof(LeafHeader);

  for (const auto& entry : leaf.entries) {
    const auto key_size = static_cast<std::uint16_t>(entry.key.size());
    const auto& record = entry.record;

    if (offset + kEntryHeaderSize + key_size > page.payload.size()) {
      throw std::runtime_error("Entry does not fit in page");
    }

    std::uint8_t tag_byte = 0;
    std::uint32_t value_len = 0;

    if (const auto* bytes = std::get_if<std::vector<std::byte>>(&record.value)) {
      tag_byte = static_cast<std::uint8_t>(EncodedValueTag::kInlineBytes);
      value_len = static_cast<std::uint32_t>(bytes->size());
    } else if (const auto* str = std::get_if<std::string>(&record.value)) {
      tag_byte = static_cast<std::uint8_t>(EncodedValueTag::kInlineString);
      value_len = static_cast<std::uint32_t>(str->size());
    } else if (std::holds_alternative<std::int64_t>(record.value)) {
      tag_byte = static_cast<std::uint8_t>(EncodedValueTag::kInlineInt64);
      value_len = sizeof(std::int64_t);
    } else {
      const auto& ref = std::get<ValueLogRef>(record.value);
      tag_byte = ref.type == ValueType::kString
                     ? static_cast<std::uint8_t>(EncodedValueTag::kValueLogString)
                     : static_cast<std::uint8_t>(EncodedValueTag::kValueLogBytes);
      value_len = ref.length;
    }

    std::memcpy(page.payload.data() + offset, &key_size, sizeof(std::uint16_t));
    offset += sizeof(std::uint16_t);

    page.payload[offset++] = static_cast<std::byte>(tag_byte);

    std::memcpy(page.payload.data() + offset, &record.metadata.ttl_epoch_seconds,
                sizeof(std::uint64_t));
    offset += sizeof(std::uint64_t);

    std::memcpy(page.payload.data() + offset, &value_len, sizeof(std::uint32_t));
    offset += sizeof(std::uint32_t);

    std::memcpy(page.payload.data() + offset, entry.key.data(), key_size);
    offset += key_size;

    if (const auto* bytes = std::get_if<std::vector<std::byte>>(&record.value)) {
      if (offset + bytes->size() > page.payload.size()) {
        throw std::runtime_error("Entry does not fit in page payload");
      }
      std::memcpy(page.payload.data() + offset, bytes->data(), bytes->size());
      offset += bytes->size();
    } else if (const auto* str = std::get_if<std::string>(&record.value)) {
      if (offset + str->size() > page.payload.size()) {
        throw std::runtime_error("Entry does not fit in page payload");
      }
      std::memcpy(page.payload.data() + offset, str->data(), str->size());
      offset += str->size();
    } else if (const auto* int_val = std::get_if<std::int64_t>(&record.value)) {
      if (offset + sizeof(std::int64_t) > page.payload.size()) {
        throw std::runtime_error("Entry does not fit in page payload");
      }
      std::memcpy(page.payload.data() + offset, int_val, sizeof(std::int64_t));
      offset += sizeof(std::int64_t);
    } else {
      const auto& ref = std::get<ValueLogRef>(record.value);
      if (offset + sizeof(ref.pointer.segment_id) + sizeof(ref.pointer.offset) +
              sizeof(ref.length) >
          page.payload.size()) {
        throw std::runtime_error("Entry does not fit in page payload");
      }
      std::memcpy(page.payload.data() + offset, &ref.pointer.segment_id,
                  sizeof(ref.pointer.segment_id));
      offset += sizeof(ref.pointer.segment_id);
      std::memcpy(page.payload.data() + offset, &ref.pointer.offset, sizeof(ref.pointer.offset));
      offset += sizeof(ref.pointer.offset);
      std::memcpy(page.payload.data() + offset, &ref.length, sizeof(ref.length));
      offset += sizeof(ref.length);
    }
  }

  return page;
}

BTree::LeafPage BTree::DecodeLeafPage(const Page& page) {
  if (page.payload.size() < sizeof(LeafHeader)) {
    throw std::runtime_error("Leaf page too small");
  }

  LeafHeader header{};
  std::memcpy(&header, page.payload.data(), sizeof(LeafHeader));
  if (header.is_leaf != 1U) {
    throw std::runtime_error("Unexpected non-leaf page during decode");
  }
  LeafPage leaf{};
  leaf.page_id = page.id;
  leaf.next_leaf = header.next_leaf;

  std::size_t offset = sizeof(LeafHeader);
  for (std::uint16_t i = 0; i < header.entry_count; ++i) {
    if (offset + kEntryHeaderSize > page.payload.size()) {
      throw std::runtime_error("Corrupt leaf entry header");
    }

    std::uint16_t key_size{};
    std::uint8_t tag_byte{};
    std::uint64_t ttl{};
    std::uint32_t value_len{};

    std::memcpy(&key_size, page.payload.data() + offset, sizeof(std::uint16_t));
    offset += sizeof(std::uint16_t);

    tag_byte = static_cast<std::uint8_t>(page.payload[offset++]);

    std::memcpy(&ttl, page.payload.data() + offset, sizeof(std::uint64_t));
    offset += sizeof(std::uint64_t);

    std::memcpy(&value_len, page.payload.data() + offset, sizeof(std::uint32_t));
    offset += sizeof(std::uint32_t);

    if (offset + key_size > page.payload.size()) {
      throw std::runtime_error("Corrupt leaf entry key");
    }
    std::string key(reinterpret_cast<const char*>(page.payload.data() + offset), key_size);
    offset += key_size;

    LeafEntry entry{};
    entry.key = std::move(key);
    entry.record.metadata.ttl_epoch_seconds = ttl;

    const auto tag = static_cast<EncodedValueTag>(tag_byte);
    switch (tag) {
    case EncodedValueTag::kInlineBytes: {
      if (offset + value_len > page.payload.size()) {
        throw std::runtime_error("Corrupt leaf entry bytes");
      }
      std::vector<std::byte> data(value_len);
      std::memcpy(data.data(), page.payload.data() + offset, value_len);
      entry.record.value = std::move(data);
      offset += value_len;
      break;
    }
    case EncodedValueTag::kInlineString: {
      if (offset + value_len > page.payload.size()) {
        throw std::runtime_error("Corrupt leaf entry string");
      }
      std::string value(reinterpret_cast<const char*>(page.payload.data() + offset), value_len);
      entry.record.value = std::move(value);
      offset += value_len;
      break;
    }
    case EncodedValueTag::kInlineInt64: {
      if (offset + sizeof(std::int64_t) > page.payload.size()) {
        throw std::runtime_error("Corrupt leaf entry int64");
      }
      std::int64_t value{};
      std::memcpy(&value, page.payload.data() + offset, sizeof(std::int64_t));
      entry.record.value = value;
      offset += sizeof(std::int64_t);
      break;
    }
    case EncodedValueTag::kValueLogBytes:
    case EncodedValueTag::kValueLogString: {
      if (offset + sizeof(vlog::SegmentPointer::segment_id) + sizeof(vlog::SegmentPointer::offset) +
              sizeof(std::uint32_t) >
          page.payload.size()) {
        throw std::runtime_error("Corrupt leaf entry value log pointer");
      }
      ValueLogRef ref{};
      std::memcpy(&ref.pointer.segment_id, page.payload.data() + offset,
                  sizeof(ref.pointer.segment_id));
      offset += sizeof(ref.pointer.segment_id);
      std::memcpy(&ref.pointer.offset, page.payload.data() + offset, sizeof(ref.pointer.offset));
      offset += sizeof(ref.pointer.offset);
      std::memcpy(&ref.length, page.payload.data() + offset, sizeof(ref.length));
      offset += sizeof(ref.length);
      ref.type = tag == EncodedValueTag::kValueLogString ? ValueType::kString : ValueType::kBytes;
      entry.record.value = ref;
      break;
    }
    default:
      throw std::runtime_error("Unknown value tag");
    }

    leaf.entries.push_back(std::move(entry));
  }

  return leaf;
}

void BTree::Persist() {
  RebuildLeafPages();
  for (const auto& leaf : leaf_pages_) {
    pager_->Write(EncodeLeafPage(leaf));
  }
}

void BTree::RebuildLeafPages() {
  std::vector<std::uint64_t> existing_ids;
  existing_ids.reserve(leaf_pages_.size());
  for (const auto& leaf : leaf_pages_) {
    existing_ids.push_back(leaf.page_id);
  }

  leaf_pages_.clear();
  const auto payload_size = pager_->payload_size();
  LeafPage current{};
  current.page_id = !existing_ids.empty() ? existing_ids.front() : root_page_id_;

  auto next_page_id = [&](std::size_t written_count) {
    if (written_count < existing_ids.size()) {
      return existing_ids[written_count];
    }
    return pager_->Allocate(PageType::kLeaf);
  };

  auto iter = in_memory_.begin();
  if (iter == in_memory_.end()) {
    current.next_leaf = kInvalidPageId;
    leaf_pages_.push_back(current);
    return;
  }

  while (iter != in_memory_.end()) {
    current.entries.clear();
    std::size_t used = sizeof(LeafHeader);
    while (iter != in_memory_.end()) {
      LeafEntry entry{iter->first, iter->second};
      const auto entry_size = EncodedEntrySize(entry);
      if (used + entry_size > payload_size) {
        break;
      }
      used += entry_size;
      current.entries.push_back(entry);
      ++iter;
    }

    if (iter != in_memory_.end()) {
      const auto next_id = next_page_id(leaf_pages_.size() + 1);
      current.next_leaf = next_id;
      leaf_pages_.push_back(current);
      LeafPage next{};
      next.page_id = next_id;
      current = next;
    } else {
      current.next_leaf = kInvalidPageId;
      leaf_pages_.push_back(current);
    }
  }
}

std::size_t BTree::EncodedEntrySize(const LeafEntry& entry) {
  const auto key_size = entry.key.size();
  std::size_t value_size = 0;
  if (const auto* bytes = std::get_if<std::vector<std::byte>>(&entry.record.value)) {
    value_size = bytes->size();
  } else if (const auto* str = std::get_if<std::string>(&entry.record.value)) {
    value_size = str->size();
  } else if (std::holds_alternative<std::int64_t>(entry.record.value)) {
    value_size = sizeof(std::int64_t);
  } else {
    value_size = sizeof(vlog::SegmentPointer::segment_id) + sizeof(vlog::SegmentPointer::offset) +
                 sizeof(std::uint32_t);
  }
  return key_size + kEntryHeaderSize + value_size;
}

Record BTree::Materialize(const LeafEntry& entry) const {
  if (const auto* ref = std::get_if<ValueLogRef>(&entry.record.value)) {
    if (value_log_ != nullptr) {
      const auto data = value_log_->Read(ref->pointer);
      if (data.has_value()) {
        Record hydrated = entry.record;
        if (ref->type == ValueType::kString) {
          hydrated.value = std::string(reinterpret_cast<const char*>(data->data()), data->size());
        } else {
          hydrated.value = *data;
        }
        return hydrated;
      }
    }
  }
  return entry.record;
}

} // namespace jubilant::storage::btree
