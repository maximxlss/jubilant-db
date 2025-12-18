#include "storage/ttl/ttl_clock.h"

#include <algorithm>
#include <chrono>
#include <limits>

namespace jubilant::storage::ttl {

namespace {

std::uint64_t ToSecondsSinceEpoch(std::chrono::system_clock::time_point time_point) {
  const auto seconds =
      std::chrono::duration_cast<std::chrono::seconds>(time_point.time_since_epoch());
  const auto count = seconds.count();
  if (count < 0) {
    return 0;
  }
  return static_cast<std::uint64_t>(count);
}

std::uint64_t ToNanoseconds(std::chrono::steady_clock::time_point time_point) {
  const auto nanos =
      std::chrono::duration_cast<std::chrono::nanoseconds>(time_point.time_since_epoch());
  const auto count = nanos.count();
  if (count < 0) {
    return 0;
  }
  return static_cast<std::uint64_t>(count);
}

std::chrono::steady_clock::time_point MakeMonotonicBase(std::uint64_t nanos_since_epoch) {
  const auto max_nanos =
      static_cast<std::uint64_t>(std::numeric_limits<std::chrono::nanoseconds::rep>::max());
  const auto clamped =
      static_cast<std::chrono::nanoseconds::rep>(std::min(nanos_since_epoch, max_nanos));
  return std::chrono::steady_clock::time_point{std::chrono::nanoseconds{clamped}};
}

} // namespace

Calibration TtlClock::CalibrateNow() {
  const auto wall_now = std::chrono::system_clock::now();
  const auto mono_now = std::chrono::steady_clock::now();
  Calibration calibration{};
  calibration.wall_clock_unix_seconds = ToSecondsSinceEpoch(wall_now);
  calibration.monotonic_time_nanos = ToNanoseconds(mono_now);
  return calibration;
}

TtlClock::TtlClock(Calibration calibration)
    : calibration_(calibration),
      monotonic_base_(MakeMonotonicBase(calibration.monotonic_time_nanos)) {}

Calibration TtlClock::calibration() const noexcept {
  return calibration_;
}

std::uint64_t TtlClock::WallNowSeconds() const noexcept {
  const auto mono_now = std::chrono::steady_clock::now();
  const auto mono_delta = mono_now - monotonic_base_;
  const auto wall_now = std::chrono::seconds{calibration_.wall_clock_unix_seconds} +
                        std::chrono::duration_cast<std::chrono::seconds>(mono_delta);
  const auto wall_seconds = wall_now.count();
  if (wall_seconds < 0) {
    return 0;
  }
  return static_cast<std::uint64_t>(wall_seconds);
}

bool TtlClock::IsExpired(std::uint64_t ttl_epoch_seconds) const noexcept {
  if (ttl_epoch_seconds == 0) {
    return false;
  }
  return ttl_epoch_seconds <= WallNowSeconds();
}

} // namespace jubilant::storage::ttl
