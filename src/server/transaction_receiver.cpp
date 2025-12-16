#include "server/transaction_receiver.h"

namespace jubilant::server {

TransactionReceiver::TransactionReceiver(std::size_t max_queue_depth)
    : max_queue_depth_(max_queue_depth) {}

bool TransactionReceiver::Enqueue(txn::TransactionRequest request) {
  std::unique_lock lock(mutex_);
  if (stopped_ || queue_.size() >= max_queue_depth_) {
    return false;
  }

  queue_.push(std::move(request));
  lock.unlock();
  cv_.notify_one();
  return true;
}

std::optional<txn::TransactionRequest> TransactionReceiver::Next() {
  std::unique_lock lock(mutex_);
  cv_.wait(lock, [this] { return stopped_ || !queue_.empty(); });
  if (queue_.empty()) {
    return std::nullopt;
  }

  auto request = std::move(queue_.front());
  queue_.pop();
  return request;
}

void TransactionReceiver::Stop() {
  {
    std::lock_guard guard(mutex_);
    stopped_ = true;
  }
  cv_.notify_all();
}

bool TransactionReceiver::stopped() const noexcept {
  std::lock_guard guard(mutex_);
  return stopped_;
}

std::size_t TransactionReceiver::backlog() const {
  std::lock_guard guard(mutex_);
  return queue_.size();
}

} // namespace jubilant::server
