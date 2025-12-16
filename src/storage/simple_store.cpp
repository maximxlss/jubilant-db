#include "storage/simple_store.h"

#include <random>
#include <stdexcept>
#include <string>

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
  const auto refreshed_superblock = superblock_store.LoadActive();
  if (refreshed_superblock.has_value()) {
    store.superblock_ = *refreshed_superblock;
  }
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

SimpleStore::Stats SimpleStore::stats() const {
  Stats stats{};
  const auto manifest = manifest_store_.Load();
  stats.manifest = manifest.value_or(manifest_);
  stats.superblock = superblock_store_.LoadActive().value_or(superblock_);
  stats.page_count = pager_.page_count();
  stats.key_count = tree_.size();
  return stats;
}

SimpleStore::ValidationReport SimpleStore::ValidateOnDisk(const std::filesystem::path& db_dir) {
  ValidationReport report{};

  meta::ManifestStore manifest_store(db_dir);
  const auto manifest = manifest_store.Load();
  if (manifest.has_value()) {
    report.has_manifest = true;
    report.manifest_result = meta::ManifestStore::Validate(*manifest);
    if (report.manifest_result.message.empty()) {
      report.manifest_result.message = "MANIFEST validated";
    }
  } else {
    report.manifest_result.ok = false;
    report.manifest_result.message = "MANIFEST missing or invalid";
  }

  meta::SuperBlockStore superblock_store(db_dir);
  const auto superblock = superblock_store.LoadActive();
  if (superblock.has_value()) {
    report.superblock_ok = true;
    report.superblock_message = "Superblock generation " + std::to_string(superblock->generation) +
                                ", root_page_id=" + std::to_string(superblock->root_page_id);

    report.checkpoint_ok = true;
    if (superblock->last_checkpoint_lsn == 0) {
      report.checkpoint_message = "No checkpoint recorded (last_checkpoint_lsn=0)";
    } else {
      report.checkpoint_message =
          "Last checkpoint LSN=" + std::to_string(superblock->last_checkpoint_lsn);
    }
  } else {
    report.superblock_message = "No valid superblock found (CRC failure or missing files)";
    report.checkpoint_message = "Checkpoint metadata unavailable";
  }

  report.ok = report.has_manifest && report.manifest_result.ok && report.superblock_ok;
  return report;
}

} // namespace jubilant::storage
