#pragma once

#include "storage/btree/btree.h"

#include <chrono>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace jubilant::cli {

struct RemoteTarget {
  std::string host;
  std::uint16_t port{0};
};

constexpr std::chrono::milliseconds kDefaultRemoteTimeout{5000};
constexpr std::uint64_t kMaxTxnId =
    static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());

RemoteTarget ParseRemoteTarget(std::string_view target);
std::uint64_t GenerateTxnId();

nlohmann::json RecordValueToEnvelope(const jubilant::storage::btree::Record& record);

nlohmann::json SendTransaction(const RemoteTarget& target, const nlohmann::json& request,
                               std::chrono::milliseconds timeout);

} // namespace jubilant::cli
