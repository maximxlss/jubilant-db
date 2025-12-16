#pragma once

#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace jubilant::lock {

enum class LockMode { kShared, kExclusive };

class LockManager {
 public:
  LockManager() = default;

  void Acquire(const std::string& key, LockMode mode);
  void Release(const std::string& key, LockMode mode);

 private:
  std::shared_mutex& MutexFor(const std::string& key);

  std::mutex map_mutex_;
  std::unordered_map<std::string, std::shared_mutex> mutexes_;
};

}  // namespace jubilant::lock
