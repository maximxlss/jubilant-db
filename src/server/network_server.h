#pragma once

#include "server/server.h"
#include "txn/transaction_request.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace jubilant::server {

class NetworkServer {
public:
  struct Config {
    std::string host{"127.0.0.1"};
    std::uint16_t port{0};
    int backlog{16};
  };

  NetworkServer(Server& core_server, Config config);
  ~NetworkServer();

  bool Start();
  void Stop();

  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] std::uint16_t port() const noexcept;

private:
  struct Connection {
    int fd{-1};
    std::string peer;
    std::thread thread;
    std::atomic<bool> active{true};
    std::atomic<bool> cleaned{false};
    std::mutex write_mutex;
    std::mutex inflight_mutex;
    std::unordered_set<std::uint64_t> inflight;
  };

  void AcceptLoop();
  void DispatchLoop();
  void HandleConnection(const std::shared_ptr<Connection>& connection);
  static bool ReadFrame(int socket_fd, std::string& payload);
  static bool WriteFrame(const std::shared_ptr<Connection>& connection, std::string_view payload);
  void CleanupConnection(const std::shared_ptr<Connection>& connection);

  bool RegisterTransaction(const std::shared_ptr<Connection>& connection, std::uint64_t txn_id);
  void ClearTransaction(const std::shared_ptr<Connection>& connection, std::uint64_t txn_id);

  static std::optional<txn::TransactionRequest> DecodeRequest(const std::string& payload);
  static std::optional<txn::TransactionRequest> DecodeRequest(const nlohmann::json& json);
  static std::optional<txn::Operation> DecodeOperation(const nlohmann::json& operation_json);
  static std::optional<storage::btree::Record> DecodeRecord(const nlohmann::json& value_json);

  static nlohmann::json EncodeResponse(const TransactionResult& result);
  static std::optional<nlohmann::json> EncodeRecord(const storage::btree::Record& record);

  static std::string EncodeBytes(const std::vector<std::byte>& data);
  static std::optional<std::vector<std::byte>> DecodeBytes(const std::string& encoded);

  static std::string OperationTypeToString(txn::OperationType type);
  static std::optional<txn::OperationType> OperationTypeFromString(std::string_view value);
  static std::string TransactionStateToString(txn::TransactionState state);

  Server& server_;
  Config config_;

  std::atomic<bool> running_{false};
  int listen_fd_{-1};
  std::uint16_t bound_port_{0};
  std::thread accept_thread_;
  std::thread dispatch_thread_;

  mutable std::mutex connections_mutex_;
  std::vector<std::shared_ptr<Connection>> connections_;
  std::unordered_map<std::uint64_t, std::weak_ptr<Connection>> pending_results_;
};

} // namespace jubilant::server
