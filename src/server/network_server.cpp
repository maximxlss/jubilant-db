#include "server/network_server.h"

#include "server/worker.h"

#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <netinet/in.h>
#include <poll.h>
#include <ranges>
#include <sys/socket.h>
#include <type_traits>
#include <unistd.h>
#include <variant>

namespace jubilant::server {
namespace {

constexpr std::size_t kMaxFrameSize = 1U << 20; // 1 MiB cap for v0.0.2
constexpr std::chrono::milliseconds kDrainWait{50};
constexpr std::chrono::milliseconds kSendTimeout{1000};

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#ifndef MSG_WAITALL
#define MSG_WAITALL 0
#endif

bool ReadExact(int socket_fd, std::byte* buffer, std::size_t length) {
  std::size_t offset = 0;
  while (offset < length) {
    const auto bytes = ::recv(socket_fd, buffer + offset, length - offset, MSG_WAITALL);
    if (bytes <= 0) {
      return false;
    }
    offset += static_cast<std::size_t>(bytes);
  }
  return true;
}

bool SendAll(int socket_fd, const std::byte* buffer, std::size_t length) {
  std::size_t offset = 0;
  const auto deadline = std::chrono::steady_clock::now() + kSendTimeout;
  while (offset < length) {
    const auto sent =
        ::send(socket_fd, buffer + offset, length - offset, MSG_NOSIGNAL | MSG_DONTWAIT);
    if (sent < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        if (std::chrono::steady_clock::now() >= deadline) {
          return false;
        }

        pollfd socket_poll{
            .fd = socket_fd,
            .events = POLLOUT,
            .revents = 0,
        };
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        const auto wait_ms =
            static_cast<int>(std::max(remaining.count(), decltype(remaining)::rep{0}));
        const auto ready = ::poll(&socket_poll, 1, wait_ms);
        if (ready <= 0 || (socket_poll.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
          return false;
        }
        continue;
      }
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (sent == 0) {
      return false;
    }
    offset += static_cast<std::size_t>(sent);
  }
  return true;
}

} // namespace

NetworkServer::NetworkServer(Server& core_server, Config config)
    : server_(core_server), config_(std::move(config)) {}

NetworkServer::~NetworkServer() {
  Stop();
}

bool NetworkServer::Start() {
  if (running_.exchange(true)) {
    return false;
  }

  if (!server_.running()) {
    running_.store(false);
    return false;
  }

  listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    running_.store(false);
    return false;
  }

  int reuse = 1;
  ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(config_.port);
  addr.sin_addr.s_addr = INADDR_ANY;
  if (!config_.host.empty() && config_.host != "0.0.0.0") {
    if (::inet_pton(AF_INET, config_.host.c_str(), &addr.sin_addr) != 1) {
      Stop();
      return false;
    }
  }

  if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    Stop();
    return false;
  }

  if (::listen(listen_fd_, config_.backlog) != 0) {
    Stop();
    return false;
  }

  sockaddr_in bound{};
  socklen_t bound_len = sizeof(bound);
  if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&bound), &bound_len) == 0) {
    bound_port_ = ntohs(bound.sin_port);
  } else {
    bound_port_ = config_.port;
  }

  accept_thread_ = std::thread([this]() { AcceptLoop(); });
  dispatch_thread_ = std::thread([this]() { DispatchLoop(); });
  return true;
}

void NetworkServer::Stop() {
  if (!running_.exchange(false)) {
    return;
  }

  if (listen_fd_ >= 0) {
    ::shutdown(listen_fd_, SHUT_RDWR);
    ::close(listen_fd_);
    listen_fd_ = -1;
  }

  {
    std::lock_guard guard(connections_mutex_);
    for (auto& connection : connections_) {
      connection->active.store(false);
      if (connection->fd >= 0) {
        ::shutdown(connection->fd, SHUT_RDWR);
      }
    }
  }

  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }
  if (dispatch_thread_.joinable()) {
    dispatch_thread_.join();
  }

  std::vector<std::shared_ptr<Connection>> to_cleanup;
  {
    std::lock_guard guard(connections_mutex_);
    to_cleanup = std::move(connections_);
    pending_results_.clear();
  }

  for (auto& connection : to_cleanup) {
    if (connection->thread.joinable()) {
      connection->thread.join();
    }
    if (connection->fd >= 0) {
      ::close(connection->fd);
      connection->fd = -1;
    }
  }
}

bool NetworkServer::running() const noexcept {
  return running_.load();
}

std::uint16_t NetworkServer::port() const noexcept {
  return bound_port_;
}

void NetworkServer::AcceptLoop() {
  while (running_.load()) {
    sockaddr_in client{};
    socklen_t client_len = sizeof(client);
    const int client_fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&client), &client_len);
    if (client_fd < 0) {
      if (!running_.load()) {
        break;
      }
      continue;
    }

    auto connection = std::make_shared<Connection>();
    connection->fd = client_fd;

    char addr_buf[INET_ADDRSTRLEN] = {0};
    if (::inet_ntop(AF_INET, &client.sin_addr, addr_buf, sizeof(addr_buf)) != nullptr) {
      connection->peer = std::string{addr_buf} + ":" + std::to_string(ntohs(client.sin_port));
    }

    {
      std::lock_guard guard(connections_mutex_);
      connections_.push_back(connection);
    }

    connection->thread = std::thread([this, connection]() { HandleConnection(connection); });
  }
}

void NetworkServer::DispatchLoop() {
  while (running_.load()) {
    server_.WaitForResults(kDrainWait);
    auto results = server_.DrainCompleted();
    if (results.empty() && !server_.running()) {
      break;
    }

    for (auto& result : results) {
      std::shared_ptr<Connection> connection;
      {
        std::lock_guard guard(connections_mutex_);
        const auto iter = pending_results_.find(result.id);
        if (iter != pending_results_.end()) {
          connection = iter->second.lock();
          pending_results_.erase(iter);
        }
      }

      if (!connection || !connection->active.load()) {
        continue;
      }

      const auto response = EncodeResponse(result);
      const auto payload = response.dump();
      if (!WriteFrame(connection, payload)) {
        CleanupConnection(connection);
        continue;
      }

      std::lock_guard inflight_guard(connection->inflight_mutex);
      connection->inflight.erase(result.id);
    }
  }
}

void NetworkServer::HandleConnection(const std::shared_ptr<Connection>& connection) {
  while (running_.load() && connection->active.load()) {
    std::string payload;
    if (!ReadFrame(connection->fd, payload)) {
      break;
    }

    const auto request = DecodeRequest(payload);
    if (!request.has_value()) {
      break;
    }
    const auto& txn_request = *request;

    if (!RegisterTransaction(connection, txn_request.id)) {
      TransactionResult duplicate{};
      duplicate.id = txn_request.id;
      duplicate.state = txn::TransactionState::kAborted;
      for (const auto& operation : txn_request.operations) {
        OperationResult operation_result{};
        operation_result.type = operation.type;
        operation_result.key = operation.key;
        operation_result.success = false;
        duplicate.operations.push_back(std::move(operation_result));
      }
      const auto response = EncodeResponse(duplicate).dump();
      WriteFrame(connection, response);
      continue;
    }

    if (!server_.SubmitTransaction(txn_request)) {
      TransactionResult rejected{};
      rejected.id = txn_request.id;
      rejected.state = txn::TransactionState::kAborted;
      for (const auto& operation : txn_request.operations) {
        OperationResult operation_result{};
        operation_result.type = operation.type;
        operation_result.key = operation.key;
        rejected.operations.push_back(std::move(operation_result));
      }
      const auto response = EncodeResponse(rejected).dump();
      WriteFrame(connection, response);
      ClearTransaction(connection, txn_request.id);
    }
  }

  CleanupConnection(connection);
}

bool NetworkServer::ReadFrame(int socket_fd, std::string& payload) {
  std::array<std::byte, 4> prefix{};
  if (!ReadExact(socket_fd, prefix.data(), prefix.size())) {
    return false;
  }

  std::uint32_t length = 0;
  std::memcpy(&length, prefix.data(), prefix.size());
  length = ntohl(length);
  if (length == 0 || length > kMaxFrameSize) {
    return false;
  }

  payload.resize(length);
  return ReadExact(socket_fd, reinterpret_cast<std::byte*>(payload.data()), length);
}

bool NetworkServer::WriteFrame(const std::shared_ptr<Connection>& connection,
                               std::string_view payload) {
  const auto length = static_cast<std::uint32_t>(payload.size());
  if (length == 0 || length > kMaxFrameSize) {
    return false;
  }

  std::uint32_t network_length = htonl(length);
  std::array<std::byte, 4> prefix{};
  std::memcpy(prefix.data(), &network_length, sizeof(network_length));

  std::scoped_lock guard(connection->write_mutex);
  if (!SendAll(connection->fd, prefix.data(), prefix.size())) {
    return false;
  }
  return SendAll(connection->fd, reinterpret_cast<const std::byte*>(payload.data()),
                 payload.size());
}

void NetworkServer::CleanupConnection(const std::shared_ptr<Connection>& connection) {
  bool expected = false;
  if (!connection->cleaned.compare_exchange_strong(expected, true)) {
    return;
  }

  connection->active.store(false);
  if (connection->fd >= 0) {
    ::shutdown(connection->fd, SHUT_RDWR);
    ::close(connection->fd);
    connection->fd = -1;
  }

  if (connection->thread.joinable()) {
    if (connection->thread.get_id() == std::this_thread::get_id()) {
      connection->thread.detach();
    } else {
      connection->thread.join();
    }
  }

  std::lock_guard guard(connections_mutex_);
  {
    std::lock_guard inflight_guard(connection->inflight_mutex);
    for (const auto txn_id : connection->inflight) {
      pending_results_.erase(txn_id);
    }
    connection->inflight.clear();
  }

  const auto iter = std::ranges::find(connections_, connection);
  if (iter != connections_.end()) {
    connections_.erase(iter);
  }
}

bool NetworkServer::RegisterTransaction(const std::shared_ptr<Connection>& connection,
                                        std::uint64_t txn_id) {
  std::lock_guard guard(connections_mutex_);
  if (pending_results_.contains(txn_id)) {
    return false;
  }

  pending_results_.insert_or_assign(txn_id, connection);
  std::lock_guard inflight_guard(connection->inflight_mutex);
  connection->inflight.insert(txn_id);
  return true;
}

void NetworkServer::ClearTransaction(const std::shared_ptr<Connection>& connection,
                                     std::uint64_t txn_id) {
  std::lock_guard guard(connections_mutex_);
  pending_results_.erase(txn_id);
  std::lock_guard inflight_guard(connection->inflight_mutex);
  connection->inflight.erase(txn_id);
}

std::optional<txn::TransactionRequest> NetworkServer::DecodeRequest(const std::string& payload) {
  const auto json = nlohmann::json::parse(payload, nullptr, false);
  if (json.is_discarded()) {
    return std::nullopt;
  }

  return DecodeRequest(json);
}

std::optional<txn::TransactionRequest> NetworkServer::DecodeRequest(const nlohmann::json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }

  const auto txn_it = json.find("txn_id");
  if (txn_it == json.end() || !txn_it->is_number_unsigned()) {
    return std::nullopt;
  }

  const auto txn_id = txn_it->get<std::uint64_t>();
  if (txn_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    return std::nullopt;
  }

  const auto operations_it = json.find("operations");
  if (operations_it == json.end() || !operations_it->is_array() || operations_it->empty()) {
    return std::nullopt;
  }

  std::vector<txn::Operation> operations;
  operations.reserve(operations_it->size());
  for (const auto& operation_json : *operations_it) {
    const auto operation = DecodeOperation(operation_json);
    if (!operation.has_value()) {
      return std::nullopt;
    }
    operations.push_back(*operation);
  }

  auto request = txn::BuildTransactionRequest(txn_id, std::move(operations));
  if (!request.Valid()) {
    return std::nullopt;
  }

  return request;
}

namespace {
std::optional<storage::btree::ValueType> DecodeValueType(std::string_view value) {
  if (value == "bytes") {
    return storage::btree::ValueType::kBytes;
  }
  if (value == "string") {
    return storage::btree::ValueType::kString;
  }
  if (value == "int") {
    return storage::btree::ValueType::kInt64;
  }
  if (value == "value_log_ref") {
    return storage::btree::ValueType::kValueLogRef;
  }
  return std::nullopt;
}
} // namespace

std::optional<txn::Operation> NetworkServer::DecodeOperation(const nlohmann::json& operation_json) {
  if (!operation_json.is_object()) {
    return std::nullopt;
  }

  const auto type_it = operation_json.find("type");
  const auto key_it = operation_json.find("key");
  if (type_it == operation_json.end() || key_it == operation_json.end()) {
    return std::nullopt;
  }
  if (!type_it->is_string() || !key_it->is_string()) {
    return std::nullopt;
  }

  const auto type = OperationTypeFromString(type_it->get<std::string>());
  if (!type.has_value()) {
    return std::nullopt;
  }

  const auto key = key_it->get<std::string>();
  if (key.empty()) {
    return std::nullopt;
  }

  txn::Operation operation{};
  operation.type = *type;
  operation.key = key;

  if (operation.type == txn::OperationType::kSet) {
    const auto value_it = operation_json.find("value");
    if (value_it == operation_json.end()) {
      return std::nullopt;
    }
    const auto record = DecodeRecord(*value_it);
    if (!record.has_value()) {
      return std::nullopt;
    }
    operation.value = *record;
  } else if (operation.type == txn::OperationType::kDelete ||
             operation.type == txn::OperationType::kAssertExists ||
             operation.type == txn::OperationType::kAssertNotExists) {
    if (operation_json.contains("value")) {
      return std::nullopt;
    }
  } else if (operation.type == txn::OperationType::kGet) {
    if (const auto value_it = operation_json.find("value"); value_it != operation_json.end()) {
      const auto record = DecodeRecord(*value_it);
      if (!record.has_value()) {
        return std::nullopt;
      }
    }
  } else if (operation.type == txn::OperationType::kAssertType) {
    const auto expected_type_it = operation_json.find("expected_type");
    if (expected_type_it == operation_json.end() || !expected_type_it->is_string()) {
      return std::nullopt;
    }
    const auto expected_type = DecodeValueType(expected_type_it->get<std::string>());
    if (!expected_type.has_value()) {
      return std::nullopt;
    }
    txn::AssertExpectation expectation{};
    expectation.expected_type = expected_type;
    operation.expected = expectation;
  } else if (operation.type == txn::OperationType::kAssertIntEq) {
    const auto expected_int_it = operation_json.find("expected_int");
    if (expected_int_it == operation_json.end() || !expected_int_it->is_number_integer()) {
      return std::nullopt;
    }
    txn::AssertExpectation expectation{};
    expectation.expected_int = expected_int_it->get<std::int64_t>();
    operation.expected = expectation;
  } else if (operation.type == txn::OperationType::kAssertBytesHashEq ||
             operation.type == txn::OperationType::kAssertStringHashEq) {
    const auto expected_hash_it = operation_json.find("expected_hash");
    if (expected_hash_it == operation_json.end() || !expected_hash_it->is_string()) {
      return std::nullopt;
    }
    txn::AssertExpectation expectation{};
    expectation.expected_hash = expected_hash_it->get<std::string>();
    operation.expected = expectation;
  }

  return operation;
}

std::optional<storage::btree::Record>
NetworkServer::DecodeRecord(const nlohmann::json& value_json) {
  if (!value_json.is_object()) {
    return std::nullopt;
  }

  const auto kind_it = value_json.find("kind");
  const auto data_it = value_json.find("data");
  if (kind_it == value_json.end() || data_it == value_json.end()) {
    return std::nullopt;
  }
  if (!kind_it->is_string()) {
    return std::nullopt;
  }

  storage::btree::Record record{};
  const auto metadata_it = value_json.find("metadata");
  if (metadata_it != value_json.end() && metadata_it->is_object()) {
    const auto ttl = metadata_it->find("ttl_epoch_seconds");
    if (ttl != metadata_it->end() && ttl->is_number_unsigned()) {
      record.metadata.ttl_epoch_seconds = ttl->get<std::uint64_t>();
    }
  }

  const auto kind = kind_it->get<std::string>();
  if (kind == "bytes") {
    if (!data_it->is_string()) {
      return std::nullopt;
    }
    const auto decoded = DecodeBytes(data_it->get<std::string>());
    if (!decoded.has_value()) {
      return std::nullopt;
    }
    record.value = *decoded;
  } else if (kind == "string") {
    if (!data_it->is_string()) {
      return std::nullopt;
    }
    record.value = data_it->get<std::string>();
  } else if (kind == "int") {
    if (!data_it->is_number_integer()) {
      return std::nullopt;
    }
    const auto number = data_it->get<std::int64_t>();
    record.value = number;
  } else {
    return std::nullopt;
  }

  return record;
}

nlohmann::json NetworkServer::EncodeResponse(const TransactionResult& result) {
  nlohmann::json json;
  json["txn_id"] = result.id;
  json["state"] = TransactionStateToString(result.state);

  nlohmann::json operations = nlohmann::json::array();
  for (const auto& op_result : result.operations) {
    nlohmann::json op_json;
    op_json["type"] = OperationTypeToString(op_result.type);
    op_json["key_id"] = op_result.key_id;
    op_json["key"] = op_result.key;
    op_json["success"] = op_result.success;
    if (op_result.value.has_value()) {
      const auto encoded = EncodeRecord(*op_result.value);
      if (encoded.has_value()) {
        op_json["value"] = *encoded;
      }
    }
    operations.push_back(std::move(op_json));
  }

  json["operations"] = std::move(operations);
  return json;
}

std::optional<nlohmann::json> NetworkServer::EncodeRecord(const storage::btree::Record& record) {
  nlohmann::json value;
  bool supported = true;

  std::visit(
      [&](const auto& stored) {
        using T = std::decay_t<decltype(stored)>;
        if constexpr (std::is_same_v<T, std::vector<std::byte>>) {
          value["kind"] = "bytes";
          value["data"] = EncodeBytes(stored);
        } else if constexpr (std::is_same_v<T, std::string>) {
          value["kind"] = "string";
          value["data"] = stored;
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
          value["kind"] = "int";
          value["data"] = stored;
        } else {
          supported = false;
        }
      },
      record.value);

  if (!supported) {
    return std::nullopt;
  }

  if (record.metadata.ttl_epoch_seconds != 0) {
    value["metadata"] = nlohmann::json::object();
    value["metadata"]["ttl_epoch_seconds"] = record.metadata.ttl_epoch_seconds;
  }

  return value;
}

std::string NetworkServer::EncodeBytes(const std::vector<std::byte>& data) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string output;
  output.reserve(((data.size() + 2U) / 3U) * 4U);

  std::uint32_t accumulator = 0;
  int bits = -6;
  for (const auto byte : data) {
    accumulator = (accumulator << 8U) + static_cast<unsigned char>(byte);
    bits += 8;
    while (bits >= 0) {
      output.push_back(kAlphabet[(accumulator >> bits) & 0x3FU]);
      bits -= 6;
    }
  }

  if (bits > -6) {
    output.push_back(kAlphabet[((accumulator << 8U) >> (bits + 8)) & 0x3FU]);
  }
  while (output.size() % 4U != 0U) {
    output.push_back('=');
  }

  return output;
}

std::optional<std::vector<std::byte>> NetworkServer::DecodeBytes(const std::string& encoded) {
  static constexpr std::array<int, 256> kReverse = [] {
    std::array<int, 256> table{};
    table.fill(-1);
    const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (std::size_t i = 0; i < alphabet.size(); ++i) {
      table[static_cast<std::size_t>(static_cast<unsigned char>(alphabet[i]))] =
          static_cast<int>(i);
    }
    return table;
  }();

  std::vector<std::byte> output;
  output.reserve((encoded.size() * 3U) / 4U);

  int accumulator = 0;
  int bits = -8;
  for (const auto encoded_char : encoded) {
    if (encoded_char == '=') {
      break;
    }

    const auto decoded =
        kReverse[static_cast<std::size_t>(static_cast<unsigned char>(encoded_char))];
    if (decoded == -1) {
      return std::nullopt;
    }

    accumulator = (accumulator << 6) + decoded;
    bits += 6;
    if (bits >= 0) {
      const auto value = static_cast<unsigned char>((accumulator >> bits) & 0xFF);
      output.push_back(static_cast<std::byte>(value));
      bits -= 8;
    }
  }

  return output;
}

std::string NetworkServer::OperationTypeToString(txn::OperationType type) {
  switch (type) {
  case txn::OperationType::kGet:
    return "get";
  case txn::OperationType::kSet:
    return "set";
  case txn::OperationType::kDelete:
    return "del";
  case txn::OperationType::kAssertExists:
    return "assert_exists";
  case txn::OperationType::kAssertNotExists:
    return "assert_not_exists";
  case txn::OperationType::kAssertType:
    return "assert_type";
  case txn::OperationType::kAssertIntEq:
    return "assert_int_eq";
  case txn::OperationType::kAssertBytesHashEq:
    return "assert_bytes_hash_eq";
  case txn::OperationType::kAssertStringHashEq:
    return "assert_string_hash_eq";
  }
  return "unknown";
}

std::optional<txn::OperationType> NetworkServer::OperationTypeFromString(std::string_view value) {
  if (value == "get") {
    return txn::OperationType::kGet;
  }
  if (value == "set") {
    return txn::OperationType::kSet;
  }
  if (value == "del" || value == "delete") {
    return txn::OperationType::kDelete;
  }
  if (value == "assert_exists") {
    return txn::OperationType::kAssertExists;
  }
  if (value == "assert_not_exists") {
    return txn::OperationType::kAssertNotExists;
  }
  if (value == "assert_type") {
    return txn::OperationType::kAssertType;
  }
  if (value == "assert_int_eq") {
    return txn::OperationType::kAssertIntEq;
  }
  if (value == "assert_bytes_hash_eq") {
    return txn::OperationType::kAssertBytesHashEq;
  }
  if (value == "assert_string_hash_eq") {
    return txn::OperationType::kAssertStringHashEq;
  }
  return std::nullopt;
}

std::string NetworkServer::TransactionStateToString(txn::TransactionState state) {
  switch (state) {
  case txn::TransactionState::kCommitted:
    return "committed";
  case txn::TransactionState::kAborted:
    return "aborted";
  case txn::TransactionState::kPending:
    return "pending";
  }
  return "pending";
}

} // namespace jubilant::server
