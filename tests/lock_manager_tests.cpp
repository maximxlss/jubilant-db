#include "lock/lock_manager.h"
#include "storage/btree/btree.h"
#include "storage/pager/pager.h"
#include "storage/vlog/value_log.h"

#include <atomic>
#include <barrier>
#include <chrono>
#include <filesystem>
#include <future>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

using jubilant::lock::LockManager;
using jubilant::lock::LockMode;
using jubilant::storage::Pager;
using jubilant::storage::btree::BTree;
using jubilant::storage::btree::Record;
using jubilant::storage::vlog::ValueLog;

namespace {

std::filesystem::path TempDir(const std::string& name) {
  const auto dir = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(dir);
  return dir;
}

} // namespace

TEST(LockManagerTest, AllowsConcurrentSharedAccess) {
  LockManager manager;

  constexpr int kThreadCount = 8;
  std::barrier sync_point{kThreadCount};
  std::atomic<int> active_readers{0};
  std::atomic<int> max_readers{0};

  auto reader = [&]() {
    sync_point.arrive_and_wait();

    manager.Acquire("key", LockMode::kShared);
    const int current = ++active_readers;

    int observed = max_readers.load();
    while (current > observed && !max_readers.compare_exchange_weak(observed, current)) {
      // observed value updated by compare_exchange_weak
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    --active_readers;
    manager.Release("key", LockMode::kShared);
  };

  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);
  for (int i = 0; i < kThreadCount; ++i) {
    threads.emplace_back(reader);
  }
  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(max_readers.load(), kThreadCount);
}

TEST(LockManagerTest, ExclusiveBlocksSharedAccess) {
  LockManager manager;
  const std::string key = "locked-key";

  std::promise<void> exclusive_acquired;
  auto ready_future = exclusive_acquired.get_future();

  std::chrono::steady_clock::time_point shared_start;
  std::chrono::steady_clock::time_point shared_acquired;

  std::thread exclusive([&]() {
    manager.Acquire(key, LockMode::kExclusive);
    exclusive_acquired.set_value();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    manager.Release(key, LockMode::kExclusive);
  });

  std::thread shared([&]() {
    ready_future.wait();
    shared_start = std::chrono::steady_clock::now();
    manager.Acquire(key, LockMode::kShared);
    shared_acquired = std::chrono::steady_clock::now();
    manager.Release(key, LockMode::kShared);
  });

  exclusive.join();
  shared.join();

  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(shared_acquired - shared_start);
  EXPECT_GE(elapsed.count(), 45);
}

TEST(LockManagerTest, SerializesConcurrentUpdatesAcrossRequests) {
  LockManager manager;
  const auto dir = TempDir("jubilant-lock-manager");
  Pager pager = Pager::Open(dir / "data.pages", jubilant::storage::kDefaultPageSize);
  ValueLog vlog(dir / "vlog");
  BTree tree(
      BTree::Config{.pager = &pager, .value_log = &vlog, .inline_threshold = 128U, .root_hint = 0});
  const std::string key = "counter";

  Record initial_record{};
  initial_record.value = std::int64_t{0};
  tree.Insert(key, initial_record);

  constexpr int kThreadCount = 6;
  constexpr int kIterations = 200;
  std::barrier sync_point{kThreadCount};

  auto worker = [&]() {
    sync_point.arrive_and_wait();

    for (int i = 0; i < kIterations; ++i) {
      manager.Acquire(key, LockMode::kExclusive);

      auto found = tree.Find(key);
      ASSERT_TRUE(found.has_value());
      if (!found.has_value()) {
        return;
      }
      const auto& current_record = found.value();
      auto current = std::get<std::int64_t>(current_record.value);

      Record updated{};
      updated.value = current + 1;
      tree.Insert(key, updated);

      manager.Release(key, LockMode::kExclusive);
    }
  };

  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);
  for (int i = 0; i < kThreadCount; ++i) {
    threads.emplace_back(worker);
  }
  for (auto& thread : threads) {
    thread.join();
  }

  auto final_record = tree.Find(key);
  ASSERT_TRUE(final_record.has_value());
  if (!final_record.has_value()) {
    return;
  }
  const auto& final_record_value = final_record.value();
  EXPECT_EQ(std::get<std::int64_t>(final_record_value.value), kThreadCount * kIterations);
}
