#include "lock/lock_manager.h"

namespace jubilant::lock {

void LockManager::Acquire(const std::string& key, LockMode mode) {
  auto& mutex = MutexFor(key);
  if (mode == LockMode::kShared) {
    mutex.lock_shared();
  } else {
    mutex.lock();
  }
}

void LockManager::Release(const std::string& key, LockMode mode) {
  std::scoped_lock guard(map_mutex_);
  auto it = mutexes_.find(key);
  if (it == mutexes_.end()) {
    return;
  }

  if (mode == LockMode::kShared) {
    it->second.unlock_shared();
  } else {
    it->second.unlock();
  }
}

std::shared_mutex& LockManager::MutexFor(const std::string& key) {
  std::scoped_lock guard(map_mutex_);
  return mutexes_[key];
}

}  // namespace jubilant::lock
