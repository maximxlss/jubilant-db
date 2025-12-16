#include "server/server.h"

#include <thread>
#include <utility>

namespace jubilant::server {

Server::Server(std::filesystem::path base_dir, std::size_t worker_count)
    : base_dir_(std::move(base_dir)), worker_count_(worker_count), wal_manager_(base_dir_),
      manifest_store_(base_dir_), superblock_store_(base_dir_) {
  manifest_record_ =
      manifest_store_.Load().value_or(meta::ManifestStore::NewDefault("server-bootstrap"));
  superblock_ = superblock_store_.LoadActive().value_or(meta::SuperBlock{});
  pager_.emplace(storage::Pager::Open(base_dir_ / "data.pages", manifest_record_.page_size));
  value_log_.emplace(base_dir_ / "vlog");
  btree_.emplace(
      storage::btree::BTree::Config{.pager = &pager_.value(),
                                    .value_log = &value_log_.value(),
                                    .inline_threshold = manifest_record_.inline_threshold,
                                    .root_hint = superblock_.root_page_id});
}

Server::~Server() {
  Stop();
}

void Server::Start() {
  if (running_.exchange(true)) {
    return;
  }

  for (std::size_t i = 0; i < worker_count_; ++i) {
    workers_.emplace_back([this]() { WorkerLoop(); });
  }
}

void Server::Stop() {
  if (!running_.exchange(false)) {
    return;
  }

  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  workers_.clear();
}

bool Server::running() const noexcept {
  return running_.load();
}

void Server::WorkerLoop() {
  // Request dispatch will be wired up after the wire protocol lands. Keeping a
  // live worker loop in place helps exercise lifecycle management in tests.
  while (running_.load()) {
    std::this_thread::yield();
  }
}

} // namespace jubilant::server
