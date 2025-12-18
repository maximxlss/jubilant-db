#pragma once

#include "meta/manifest.h"
#include "meta/superblock.h"
#include "storage/btree/btree.h"
#include "storage/pager/pager.h"
#include "storage/ttl/ttl_clock.h"
#include "storage/vlog/value_log.h"

#include <filesystem>
#include <optional>
#include <string>

namespace jubilant::storage {

class SimpleStore {
public:
  static SimpleStore Open(const std::filesystem::path& db_dir);

  [[nodiscard]] std::optional<btree::Record> Get(const std::string& key) const;
  void Set(const std::string& key, btree::Record record);
  bool Delete(const std::string& key);

  void Sync();

  [[nodiscard]] std::uint64_t size() const noexcept;

  struct Stats {
    meta::ManifestRecord manifest;
    meta::SuperBlock superblock;
    std::uint64_t page_count{0};
    std::uint64_t key_count{0};
  };

  [[nodiscard]] Stats stats() const;

  struct ValidationReport {
    bool ok{false};
    meta::ManifestValidationResult manifest_result{};
    bool has_manifest{false};
    bool superblock_ok{false};
    std::string superblock_message;
    bool checkpoint_ok{false};
    std::string checkpoint_message;
  };

  [[nodiscard]] static ValidationReport ValidateOnDisk(const std::filesystem::path& db_dir);

private:
  SimpleStore(std::filesystem::path db_dir, meta::ManifestRecord manifest,
              meta::SuperBlock superblock, Pager pager, vlog::ValueLog value_log);

  void RefreshRoot();

  std::filesystem::path db_dir_;
  meta::ManifestStore manifest_store_;
  meta::SuperBlockStore superblock_store_;
  meta::ManifestRecord manifest_;
  meta::SuperBlock superblock_;
  Pager pager_;
  vlog::ValueLog value_log_;
  std::optional<ttl::TtlClock> ttl_clock_;
  btree::BTree tree_;
};

} // namespace jubilant::storage
