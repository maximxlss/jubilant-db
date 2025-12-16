#include "server/server.h"

#include <thread>
#include <utility>

namespace jubilant::server {

Server::Server(std::filesystem::path base_dir, std::size_t worker_count)
    : base_dir_(std::move(base_dir)),
      worker_count_(worker_count),
      wal_manager_(base_dir_),
      manifest_store_(base_dir_),
      superblock_store_(base_dir_) {}

Server::~Server() { Stop(); }

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

bool Server::running() const noexcept { return running_.load(); }

void Server::WorkerLoop() {
  // Request dispatch will be wired up after the wire protocol lands. Keeping a
  // live worker loop in place helps exercise lifecycle management in tests.
  while (running_.load()) {
    std::this_thread::yield();
  }
}

}  // namespace jubilant::server
