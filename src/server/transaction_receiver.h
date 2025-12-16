#pragma once

#include "txn/transaction_request.h"

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>

namespace jubilant::server {

class TransactionReceiver {
public:
  explicit TransactionReceiver(std::size_t max_queue_depth = 1024);

  bool Enqueue(txn::TransactionRequest request);
  std::optional<txn::TransactionRequest> Next();
  void Stop();

  [[nodiscard]] bool stopped() const noexcept;
  [[nodiscard]] std::size_t backlog() const;

private:
  std::size_t max_queue_depth_{0};
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<txn::TransactionRequest> queue_;
  bool stopped_{false};
};

} // namespace jubilant::server
