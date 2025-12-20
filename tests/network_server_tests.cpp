#include "server/network_server.h"
#include "server/server.h"

#include <arpa/inet.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/time.h>
#include <system_error>
#include <unistd.h>

using jubilant::server::NetworkServer;
using jubilant::server::Server;

namespace {

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#ifndef MSG_WAITALL
#define MSG_WAITALL 0
#endif

bool SendAll(int socket_fd, const std::byte* buffer, std::size_t length) {
  std::size_t offset = 0;
  while (offset < length) {
    const auto sent = ::send(socket_fd, buffer + offset, length - offset, MSG_NOSIGNAL);
    if (sent <= 0) {
      return false;
    }
    offset += static_cast<std::size_t>(sent);
  }
  return true;
}

bool WriteFrame(int socket_fd, const nlohmann::json& json) {
  const auto payload = json.dump();
  const auto length = static_cast<std::uint32_t>(payload.size());
  const auto network_length = htonl(length);

  if (!SendAll(socket_fd, reinterpret_cast<const std::byte*>(&network_length),
               sizeof(network_length))) {
    return false;
  }
  return SendAll(socket_fd, reinterpret_cast<const std::byte*>(payload.data()), payload.size());
}

std::optional<std::string> ReadFrame(int socket_fd) {
  std::array<std::byte, 4> prefix{};
  std::size_t offset = 0;
  while (offset < prefix.size()) {
    const auto bytes = ::recv(socket_fd, prefix.data() + offset, prefix.size() - offset, 0);
    if (bytes <= 0) {
      return std::nullopt;
    }
    offset += static_cast<std::size_t>(bytes);
  }

  std::uint32_t length = 0;
  std::memcpy(&length, prefix.data(), prefix.size());
  length = ntohl(length);
  if (length == 0 || length > (1U << 20)) {
    return std::nullopt;
  }

  std::string payload(length, '\0');
  offset = 0;
  while (offset < payload.size()) {
    const auto bytes =
        ::recv(socket_fd, payload.data() + offset, payload.size() - offset, MSG_WAITALL);
    if (bytes <= 0) {
      return std::nullopt;
    }
    offset += static_cast<std::size_t>(bytes);
  }

  return payload;
}

std::optional<nlohmann::json> ReadJsonFrame(int socket_fd) {
  const auto payload = ReadFrame(socket_fd);
  if (!payload.has_value()) {
    return std::nullopt;
  }

  auto json = nlohmann::json::parse(*payload, nullptr, false);
  if (json.is_discarded()) {
    return std::nullopt;
  }
  return json;
}

struct TempDirGuard {
  explicit TempDirGuard(std::string_view prefix)
      : path(std::filesystem::temp_directory_path() /
             std::filesystem::path(std::string(prefix) + "-" +
                                   std::to_string(std::random_device{}()))) {
    std::filesystem::create_directories(path);
  }

  ~TempDirGuard() {
    std::error_code error_code;
    std::filesystem::remove_all(path, error_code);
  }

  std::filesystem::path path;
};

struct ServerGuard {
  jubilant::server::Server& core;
  jubilant::server::NetworkServer& network;

  ~ServerGuard() {
    network.Stop();
    core.Stop();
  }
};

} // namespace

TEST(NetworkServerTest, ExecutesTransactionsOverTcp) {
  TempDirGuard dir{"jubilant-network-server"};

  Server core_server{dir.path, 2};
  core_server.Start();

  NetworkServer::Config config{};
  config.host = "127.0.0.1";
  config.port = 0;
  NetworkServer network{core_server, config};
  const ServerGuard guard{.core = core_server, .network = network};
  ASSERT_TRUE(network.Start());
  ASSERT_GT(network.port(), 0);

  const int socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(socket_fd, 0);

  timeval timeout{};
  timeout.tv_sec = 2;
  timeout.tv_usec = 0;
  ::setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(network.port());
  ASSERT_EQ(::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr), 1);
  ASSERT_EQ(::connect(socket_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);

  nlohmann::json set_request;
  set_request["txn_id"] = 1;
  set_request["operations"] = nlohmann::json::array();
  set_request["operations"].push_back(
      {{"type", "set"}, {"key", "alpha"}, {"value", {{"kind", "string"}, {"data", "bravo"}}}});

  ASSERT_TRUE(WriteFrame(socket_fd, set_request));
  const auto set_response = ReadJsonFrame(socket_fd);
  ASSERT_TRUE(set_response.has_value());
  if (!set_response.has_value()) {
    return;
  }
  EXPECT_EQ(set_response->at("txn_id"), 1);
  EXPECT_EQ(set_response->at("state"), "committed");
  ASSERT_TRUE(set_response->contains("operations"));
  ASSERT_EQ(set_response->at("operations").size(), 1);
  EXPECT_TRUE(set_response->at("operations")[0].at("success").get<bool>());

  nlohmann::json get_request;
  get_request["txn_id"] = 2;
  get_request["operations"] = nlohmann::json::array();
  get_request["operations"].push_back({{"type", "get"}, {"key", "alpha"}});

  ASSERT_TRUE(WriteFrame(socket_fd, get_request));
  const auto get_response = ReadJsonFrame(socket_fd);
  ASSERT_TRUE(get_response.has_value());
  if (!get_response.has_value()) {
    return;
  }
  EXPECT_EQ(get_response->at("txn_id"), 2);
  EXPECT_EQ(get_response->at("state"), "committed");
  ASSERT_EQ(get_response->at("operations").size(), 1);
  const auto& operation_json = get_response->at("operations")[0];
  EXPECT_TRUE(operation_json.at("success").get<bool>());
  ASSERT_TRUE(operation_json.contains("value"));
  EXPECT_EQ(operation_json.at("value").at("kind"), "string");
  EXPECT_EQ(operation_json.at("value").at("data"), "bravo");

  ::close(socket_fd);
}

TEST(NetworkServerTest, ClosesConnectionWhenResponseExceedsCap) {
  TempDirGuard dir{"jubilant-network-server-cap"};

  Server core_server{dir.path, 2};
  core_server.Start();

  // Seed a value larger than 1 MiB directly through the core server.
  std::string large_value(1'100'000, 'x');
  jubilant::storage::btree::Record large_record{};
  large_record.value = large_value;

  jubilant::txn::Operation set_operation{
      .type = jubilant::txn::OperationType::kSet, .key = "oversized", .value = large_record};
  jubilant::txn::TransactionRequest seed_request =
      jubilant::txn::BuildTransactionRequest(11, {set_operation});
  ASSERT_TRUE(core_server.SubmitTransaction(seed_request));

  bool seeded = false;
  for (int attempt = 0; attempt < 50 && !seeded; ++attempt) {
    core_server.WaitForResults(std::chrono::milliseconds(10));
    auto seeded_results = core_server.DrainCompleted();
    if (!seeded_results.empty()) {
      seeded = true;
      EXPECT_EQ(seeded_results.front().state, jubilant::txn::TransactionState::kCommitted);
    }
  }
  ASSERT_TRUE(seeded);

  NetworkServer::Config config{};
  config.host = "127.0.0.1";
  config.port = 0;
  NetworkServer network{core_server, config};
  const ServerGuard guard{.core = core_server, .network = network};
  ASSERT_TRUE(network.Start());
  ASSERT_GT(network.port(), 0);

  const int socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(socket_fd, 0);

  timeval timeout{};
  timeout.tv_sec = 2;
  timeout.tv_usec = 0;
  ::setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(network.port());
  ASSERT_EQ(::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr), 1);
  ASSERT_EQ(::connect(socket_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);

  nlohmann::json get_request;
  get_request["txn_id"] = 12;
  get_request["operations"] = nlohmann::json::array();
  get_request["operations"].push_back({{"type", "get"}, {"key", "oversized"}});

  ASSERT_TRUE(WriteFrame(socket_fd, get_request));
  const auto get_response = ReadJsonFrame(socket_fd);
  EXPECT_FALSE(get_response.has_value());

  ::close(socket_fd);
}
