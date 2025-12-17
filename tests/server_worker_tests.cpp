#include "lock/lock_manager.h"
#include "server/server.h"
#include "server/transaction_receiver.h"
#include "server/worker.h"
#include "txn/transaction_request.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <gtest/gtest.h>
#include <iterator>
#include <optional>
#include <shared_mutex>
#include <thread>
#include <variant>

using jubilant::lock::LockManager;
using jubilant::server::Server;
using jubilant::server::TransactionReceiver;
using jubilant::server::TransactionResult;
using jubilant::server::Worker;
using jubilant::storage::Pager;
using jubilant::storage::btree::Record;
using jubilant::storage::vlog::ValueLog;
using jubilant::txn::Operation;
using jubilant::txn::OperationType;
using jubilant::txn::TransactionRequest;
using jubilant::txn::TransactionState;

TEST(TransactionReceiverTest, UnblocksOnStop) {
  TransactionReceiver receiver{};
  std::atomic<bool> woke{false};

  std::thread consumer([&]() {
    const auto next = receiver.Next();
    woke = true;
    EXPECT_FALSE(next.has_value());
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  receiver.Stop();
  consumer.join();

  EXPECT_TRUE(receiver.stopped());
  EXPECT_TRUE(woke.load());
}

TEST(WorkerTest, ProcessesOperationsAndEmitsResults) {
  TransactionReceiver receiver{};
  LockManager lock_manager{};
  const auto dir = std::filesystem::temp_directory_path() / "jubilant-worker-btree";
  std::filesystem::remove_all(dir);
  Pager pager = Pager::Open(dir / "data.pages", jubilant::storage::kDefaultPageSize);
  ValueLog vlog(dir / "vlog");
  jubilant::storage::btree::BTree btree(jubilant::storage::btree::BTree::Config{
      .pager = &pager, .value_log = &vlog, .inline_threshold = 128U, .root_hint = 0});
  std::shared_mutex btree_mutex;

  std::mutex results_mutex;
  std::condition_variable results_cv;
  std::vector<TransactionResult> results;

  Worker worker{
      "worker-0", receiver, lock_manager, btree, btree_mutex, [&](TransactionResult result) {
        std::lock_guard guard(results_mutex);
        results.push_back(std::move(result));
        results_cv.notify_all();
      }};
  worker.Start();

  Record record{};
  record.value = std::string{"value"};

  Operation set_op{.type = OperationType::kSet, .key = "alpha", .value = record};
  Operation get_op{.type = OperationType::kGet, .key = "alpha", .value = std::nullopt};
  Operation delete_op{.type = OperationType::kDelete, .key = "alpha", .value = std::nullopt};

  TransactionRequest request{.id = 1, .operations = {set_op, get_op, delete_op}};
  ASSERT_TRUE(receiver.Enqueue(request));

  std::unique_lock results_lock{results_mutex};
  ASSERT_TRUE(results_cv.wait_for(results_lock, std::chrono::milliseconds(200),
                                  [&results]() { return !results.empty(); }));
  results_lock.unlock();

  receiver.Stop();
  worker.Stop();

  ASSERT_FALSE(results.empty());
  const auto& result = results.front();
  EXPECT_EQ(result.state, TransactionState::kCommitted);
  ASSERT_EQ(result.operations.size(), 3U);

  const auto& read_result = result.operations[1];
  EXPECT_EQ(read_result.type, OperationType::kGet);
  EXPECT_TRUE(read_result.success);
  ASSERT_TRUE(read_result.value.has_value());
  if (!read_result.value.has_value()) {
    return;
  }
  EXPECT_EQ(std::get<std::string>(read_result.value->value), "value");

  EXPECT_FALSE(btree.Find("alpha").has_value());
}

TEST(ServerTest, SubmitsAndDrainsTransactions) {
  const auto temp_dir = std::filesystem::temp_directory_path() / "jubilant-server-scaffold";
  std::filesystem::remove_all(temp_dir);

  Server server{temp_dir, 2};
  server.Start();

  Record record{};
  record.value = static_cast<std::int64_t>(99);
  Operation set_op{.type = OperationType::kSet, .key = "key", .value = record};
  TransactionRequest request{.id = 7, .operations = {set_op}};

  ASSERT_TRUE(server.SubmitTransaction(request));

  std::vector<TransactionResult> drained;
  for (int i = 0; i < 20 && drained.empty(); ++i) {
    auto chunk = server.DrainCompleted();
    drained.insert(drained.end(), std::make_move_iterator(chunk.begin()),
                   std::make_move_iterator(chunk.end()));
    if (!drained.empty()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  server.Stop();

  ASSERT_FALSE(drained.empty());
  const auto& result = drained.front();
  EXPECT_EQ(result.id, 7U);
  EXPECT_EQ(result.state, TransactionState::kCommitted);
}
