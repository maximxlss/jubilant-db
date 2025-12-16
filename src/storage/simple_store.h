#pragma once

#include "meta/manifest.h"
#include "meta/superblock.h"
#include "storage/btree/btree.h"
#include "storage/pager/pager.h"
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
  btree::BTree tree_;
};

} // namespace jubilant::storage
