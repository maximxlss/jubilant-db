#include "server/server.h"

#include <string>
#include <utility>

namespace jubilant::server {

Server::Server(std::filesystem::path base_dir, std::size_t worker_count)
    : base_dir_(std::move(base_dir)), worker_count_(worker_count), wal_manager_(base_dir_),
      manifest_store_(base_dir_), superblock_store_(base_dir_) {}

Server::~Server() {
  Stop();
}

void Server::Start() {
  if (running_.exchange(true)) {
    return;
  }

  for (std::size_t i = 0; i < worker_count_; ++i) {
    auto on_complete = [this](TransactionResult result) {
      std::scoped_lock guard(results_mutex_);
      completed_transactions_.push_back(std::move(result));
    };

    auto worker = std::make_unique<Worker>("worker-" + std::to_string(i), receiver_, lock_manager_,
                                           btree_, btree_mutex_, on_complete);
    worker->Start();
    workers_.push_back(std::move(worker));
  }
}

void Server::Stop() {
  if (!running_.exchange(false)) {
    return;
  }

  receiver_.Stop();

  for (auto& worker : workers_) {
    worker->Stop();
  }
  workers_.clear();
}

bool Server::SubmitTransaction(txn::TransactionRequest request) {
  if (!running()) {
    return false;
  }
  if (!request.Valid()) {
    return false;
  }

  return receiver_.Enqueue(std::move(request));
}

std::vector<TransactionResult> Server::DrainCompleted() {
  std::scoped_lock guard(results_mutex_);
  auto drained = std::move(completed_transactions_);
  completed_transactions_.clear();
  return drained;
}

bool Server::running() const noexcept {
  return running_.load();
}

} // namespace jubilant::server
