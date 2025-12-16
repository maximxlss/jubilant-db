#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "meta/manifest.h"
#include "meta/superblock.h"
#include "storage/btree/btree.h"
#include "storage/pager/pager.h"

namespace jubilant::storage {

// A minimal durable key-value store for v0.0.1. Records are appended as pages
// in the pager; the in-memory B-Tree facade provides overwrite + tombstone
// semantics without a full on-disk tree layout yet.
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
              meta::SuperBlock superblock, Pager pager);

  void LoadFromPages();
  void AppendRecordPage(const std::string& key, const btree::Record& record, bool tombstone);
  [[nodiscard]] std::vector<std::byte> EncodeRecord(const std::string& key,
                                                    const btree::Record& record, bool tombstone);
  [[nodiscard]] static std::optional<std::pair<std::string, btree::Record>> DecodeRecord(
      const std::vector<std::byte>& payload, bool& tombstone);

  std::filesystem::path db_dir_;
  meta::ManifestStore manifest_store_;
  meta::SuperBlockStore superblock_store_;
  meta::ManifestRecord manifest_;
  meta::SuperBlock superblock_;
  Pager pager_;
  btree::BTree tree_;
};

}  // namespace jubilant::storage
