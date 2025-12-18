#include "server/server.h"

#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace jubilant::server {

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

std::size_t ResolveWorkerCount(std::size_t requested) {
  if (requested > 0) {
    return requested;
  }
  const auto hardware = std::thread::hardware_concurrency();
  return hardware > 0 ? static_cast<std::size_t>(hardware) : 1U;
}

meta::ManifestRecord LoadOrCreateManifest(meta::ManifestStore& manifest_store,
                                          const config::Config& config) {
  auto manifest = manifest_store.Load();
  if (manifest.has_value()) {
    return *manifest;
  }

  manifest = meta::ManifestStore::NewDefault(GenerateUuidLikeString());
  manifest->page_size = config.page_size;
  manifest->inline_threshold = config.inline_threshold;

  if (!manifest_store.Persist(*manifest)) {
    throw std::runtime_error("Failed to persist MANIFEST");
  }
  return *manifest;
}

meta::SuperBlock LoadOrCreateSuperblock(meta::SuperBlockStore& superblock_store,
                                        meta::SuperBlock superblock,
                                        const storage::btree::BTree& btree,
                                        const storage::ttl::Calibration& ttl_calibration) {
  bool needs_write = false;
  if (superblock.generation == 0) {
    superblock.root_page_id = btree.root_page_id();
    needs_write = true;
  }

  if (superblock.ttl_calibration.wall_base != ttl_calibration.wall_clock_unix_seconds ||
      superblock.ttl_calibration.mono_base != ttl_calibration.monotonic_time_nanos) {
    superblock.ttl_calibration.wall_base = ttl_calibration.wall_clock_unix_seconds;
    superblock.ttl_calibration.mono_base = ttl_calibration.monotonic_time_nanos;
    needs_write = true;
  }

  if (needs_write && superblock_store.WriteNext(superblock)) {
    const auto refreshed = superblock_store.LoadActive();
    if (refreshed.has_value()) {
      return *refreshed;
    }
  }
  return superblock;
}

} // namespace

Server::Server(std::filesystem::path base_dir, std::size_t worker_count)
    : Server(config::ConfigLoader::Default(std::move(base_dir)), worker_count) {}

Server::Server(const config::Config& config, std::size_t worker_count)
    : base_dir_(config.db_path), worker_count_(ResolveWorkerCount(worker_count)),
      wal_manager_(base_dir_), manifest_store_(base_dir_), superblock_store_(base_dir_) {
  std::filesystem::create_directories(base_dir_);
  manifest_record_ = LoadOrCreateManifest(manifest_store_, config);
  superblock_ = superblock_store_.LoadActive().value_or(meta::SuperBlock{});
  const auto ttl_calibration = storage::ttl::TtlClock::CalibrateNow();
  ttl_clock_.emplace(ttl_calibration);
  pager_.emplace(storage::Pager::Open(base_dir_ / "data.pages", manifest_record_.page_size));
  value_log_.emplace(base_dir_ / "vlog");
  auto& btree = btree_.emplace(
      storage::btree::BTree::Config{.pager = &pager_.value(),
                                    .value_log = &value_log_.value(),
                                    .inline_threshold = manifest_record_.inline_threshold,
                                    .root_hint = superblock_.root_page_id,
                                    .ttl_clock = ttl_clock_ ? &ttl_clock_.value() : nullptr});
  superblock_ = LoadOrCreateSuperblock(superblock_store_, superblock_, btree, ttl_calibration);
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
