#include "storage/simple_store.h"

#include <random>
#include <stdexcept>

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
                         meta::SuperBlock superblock, Pager pager, vlog::ValueLog value_log)
    : db_dir_(std::move(db_dir)), manifest_store_(db_dir_), superblock_store_(db_dir_),
      manifest_(std::move(manifest)), superblock_(superblock), pager_(std::move(pager)),
      value_log_(std::move(value_log)),
      tree_(btree::BTree::Config{.pager = &pager_,
                                 .value_log = &value_log_,
                                 .inline_threshold = manifest_.inline_threshold,
                                 .root_hint = superblock_.root_page_id}) {
  RefreshRoot();
}

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
  vlog::ValueLog value_log(db_dir / "vlog");

  SimpleStore store(db_dir, *manifest, superblock, std::move(pager), std::move(value_log));
  superblock_store.WriteNext(store.superblock_);
  return store;
}

void SimpleStore::RefreshRoot() {
  superblock_.root_page_id = tree_.root_page_id();
}

std::optional<btree::Record> SimpleStore::Get(const std::string& key) const {
  return tree_.Find(key);
}

void SimpleStore::Set(const std::string& key, btree::Record record) {
  if (key.empty()) {
    throw std::invalid_argument("Key must not be empty");
  }

  tree_.Insert(key, std::move(record));
  RefreshRoot();
}

bool SimpleStore::Delete(const std::string& key) {
  if (key.empty()) {
    throw std::invalid_argument("Key must not be empty");
  }

  const bool erased = tree_.Erase(key);
  if (erased) {
    RefreshRoot();
  }
  return erased;
}

void SimpleStore::Sync() {
  pager_.Sync();
  manifest_store_.Persist(manifest_);
  superblock_store_.WriteNext(superblock_);
}

std::uint64_t SimpleStore::size() const noexcept {
  return tree_.size();
}

} // namespace jubilant::storage
