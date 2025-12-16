#pragma once

#include "lock/lock_manager.h"
#include "meta/manifest.h"
#include "meta/superblock.h"
#include "storage/btree/btree.h"
#include "storage/pager/pager.h"
#include "storage/vlog/value_log.h"
#include "storage/wal/wal_manager.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace jubilant::server {

class Server {
public:
  Server(std::filesystem::path base_dir, std::size_t worker_count);
  ~Server();

  void Start();
  void Stop();

  [[nodiscard]] bool running() const noexcept;

private:
  void WorkerLoop();

  std::filesystem::path base_dir_;
  std::size_t worker_count_{0};
  std::atomic<bool> running_{false};

  lock::LockManager lock_manager_;
  std::optional<storage::Pager> pager_;
  std::optional<storage::vlog::ValueLog> value_log_;
  std::optional<storage::btree::BTree> btree_;
  storage::wal::WalManager wal_manager_;
  meta::ManifestStore manifest_store_;
  meta::SuperBlockStore superblock_store_;
  meta::ManifestRecord manifest_record_{};
  meta::SuperBlock superblock_{};

  std::vector<std::thread> workers_;
};

} // namespace jubilant::server
