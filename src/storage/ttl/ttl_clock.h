#pragma once

#include <chrono>
#include <cstdint>

namespace jubilant::storage::ttl {

struct Calibration {
  std::uint64_t wall_clock_unix_seconds{0};
  std::uint64_t monotonic_time_nanos{0};
};

class TtlClock {
public:
  static Calibration CalibrateNow();

  explicit TtlClock(Calibration calibration);

  [[nodiscard]] Calibration calibration() const noexcept;
  [[nodiscard]] std::uint64_t WallNowSeconds() const noexcept;
  [[nodiscard]] bool IsExpired(std::uint64_t ttl_epoch_seconds) const noexcept;

private:
  Calibration calibration_{};
  std::chrono::steady_clock::time_point monotonic_base_;
};

} // namespace jubilant::storage::ttl
