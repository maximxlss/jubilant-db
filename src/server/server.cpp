#include "server/server.h"

#include <stdexcept>
#include <string>
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

  if (!btree_) {
    throw std::logic_error("B-tree not initialized");
  }

  auto& btree = *btree_;

  for (std::size_t i = 0; i < worker_count_; ++i) {
    auto on_complete = [this](TransactionResult result) {
      std::scoped_lock guard(results_mutex_);
      completed_transactions_.push_back(std::move(result));
      results_cv_.notify_all();
    };

    auto worker = std::make_unique<Worker>("worker-" + std::to_string(i), receiver_, lock_manager_,
                                           btree, btree_mutex_, on_complete);
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

  results_cv_.notify_all();
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

bool Server::WaitForResults(std::chrono::milliseconds timeout) {
  std::unique_lock lock(results_mutex_);
  return results_cv_.wait_for(
      lock, timeout, [this]() { return !completed_transactions_.empty() || !running_.load(); });
}

bool Server::running() const noexcept {
  return running_.load();
}

} // namespace jubilant::server
