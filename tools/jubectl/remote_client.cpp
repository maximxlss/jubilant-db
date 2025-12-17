#include "remote_client.h"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <memory>
#include <netdb.h>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/time.h>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace jubilant::cli {
namespace {

constexpr std::size_t kMaxFrameBytes = 1U << 20U;

std::string Base64Encode(std::span<const std::byte> input) {
  static constexpr char kEncodingTable[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string output;
  output.reserve(((input.size() + 2U) / 3U) * 4U);

  std::size_t index = 0;
  while (index + 3U <= input.size()) {
    const auto byte0 = std::to_integer<unsigned char>(input[index]);
    const auto byte1 = std::to_integer<unsigned char>(input[index + 1U]);
    const auto byte2 = std::to_integer<unsigned char>(input[index + 2U]);

    output.push_back(kEncodingTable[byte0 >> 2U]);
    output.push_back(kEncodingTable[((byte0 & 0x03U) << 4U) | (byte1 >> 4U)]);
    output.push_back(kEncodingTable[((byte1 & 0x0FU) << 2U) | (byte2 >> 6U)]);
    output.push_back(kEncodingTable[byte2 & 0x3FU]);

    index += 3U;
  }

  if (index < input.size()) {
    const auto byte0 = std::to_integer<unsigned char>(input[index]);
    const unsigned char byte1 =
        (index + 1U < input.size()) ? std::to_integer<unsigned char>(input[index + 1U]) : 0U;
    const bool has_second = (index + 1U < input.size());

    output.push_back(kEncodingTable[byte0 >> 2U]);
    output.push_back(kEncodingTable[((byte0 & 0x03U) << 4U) | (byte1 >> 4U)]);
    if (has_second) {
      output.push_back(kEncodingTable[(byte1 & 0x0FU) << 2U]);
      output.push_back('=');
    } else {
      output.push_back('=');
      output.push_back('=');
    }
  }

  return output;
}

void SetSocketTimeout(int socket_fd, std::chrono::milliseconds timeout) {
  const struct timeval timeout_value{
      .tv_sec = static_cast<time_t>(timeout.count() / 1000),
      .tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000),
  };

  if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout_value, sizeof(timeout_value)) != 0 ||
      setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout_value, sizeof(timeout_value)) != 0) {
    throw std::system_error(errno, std::system_category(), "setsockopt failed");
  }
}

int OpenSocket(const RemoteTarget& target, std::chrono::milliseconds timeout) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo* result = nullptr;
  const auto port_string = std::to_string(target.port);
  const int result_code = getaddrinfo(target.host.c_str(), port_string.c_str(), &hints, &result);
  if (result_code != 0) {
    throw std::runtime_error(std::string{"getaddrinfo failed: "} + gai_strerror(result_code));
  }

  int socket_fd = -1;
  for (addrinfo* entry = result; entry != nullptr; entry = entry->ai_next) {
    socket_fd = socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
    if (socket_fd < 0) {
      continue;
    }

    try {
      SetSocketTimeout(socket_fd, timeout);
      if (connect(socket_fd, entry->ai_addr, entry->ai_addrlen) == 0) {
        break;
      }
    } catch (...) {
      close(socket_fd);
      socket_fd = -1;
      continue;
    }

    close(socket_fd);
    socket_fd = -1;
  }

  freeaddrinfo(result);

  if (socket_fd < 0) {
    throw std::runtime_error("Failed to connect to remote target");
  }

  return socket_fd;
}

void WriteAll(int socket_fd, std::string_view buffer) {
  std::size_t offset = 0;
  while (offset < buffer.size()) {
    const auto bytes_sent =
        send(socket_fd, buffer.data() + static_cast<ptrdiff_t>(offset), buffer.size() - offset, 0);
    if (bytes_sent < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::system_error(errno, std::system_category(), "send failed");
    }
    if (bytes_sent == 0) {
      throw std::runtime_error("connection closed while sending");
    }
    offset += static_cast<std::size_t>(bytes_sent);
  }
}

std::string ReadExact(int socket_fd, std::size_t length) {
  std::string buffer(length, '\0');
  std::size_t offset = 0;
  while (offset < length) {
    const auto bytes_read =
        recv(socket_fd, buffer.data() + static_cast<ptrdiff_t>(offset), length - offset, 0);
    if (bytes_read < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::system_error(errno, std::system_category(), "recv failed");
    }
    if (bytes_read == 0) {
      buffer.resize(offset);
      break;
    }
    offset += static_cast<std::size_t>(bytes_read);
  }
  return buffer;
}

[[nodiscard]] nlohmann::json ParseJson(std::string_view body) {
  try {
    return nlohmann::json::parse(body);
  } catch (const nlohmann::json::parse_error& err) {
    throw std::runtime_error(std::string{"Invalid JSON payload: "} + err.what());
  }
}

} // namespace

RemoteTarget ParseRemoteTarget(std::string_view target) {
  const auto colon_pos = target.rfind(':');
  if (colon_pos == std::string_view::npos || colon_pos == 0U || colon_pos == target.size() - 1U) {
    throw std::invalid_argument("--remote must be host:port");
  }

  RemoteTarget parsed{};
  parsed.host = std::string{target.substr(0, colon_pos)};
  const auto port_view = target.substr(colon_pos + 1U);

  try {
    const auto port_value = std::stoul(std::string{port_view});
    if (port_value == 0U || port_value > 65535U) {
      throw std::invalid_argument("port must be within 1-65535");
    }
    parsed.port = static_cast<std::uint16_t>(port_value);
  } catch (const std::exception& ex) {
    throw std::invalid_argument(std::string{"invalid port: "} + ex.what());
  }

  return parsed;
}

std::uint64_t GenerateTxnId() {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<std::uint64_t> dist(0, kMaxTxnId);
  return dist(rng);
}

nlohmann::json RecordValueToEnvelope(const jubilant::storage::btree::Record& record) {
  const auto& value = record.value;
  nlohmann::json encoded;

  if (std::holds_alternative<std::vector<std::byte>>(value)) {
    const auto& bytes = std::get<std::vector<std::byte>>(value);
    encoded["kind"] = "bytes";
    encoded["data"] = Base64Encode(bytes);
  } else if (std::holds_alternative<std::string>(value)) {
    encoded["kind"] = "string";
    encoded["data"] = std::get<std::string>(value);
  } else if (std::holds_alternative<std::int64_t>(value)) {
    encoded["kind"] = "int";
    encoded["data"] = std::get<std::int64_t>(value);
  } else {
    throw std::invalid_argument("Unsupported record value for remote envelope");
  }

  if (record.metadata.ttl_epoch_seconds != 0U) {
    encoded["metadata"] = {{"ttl_epoch_seconds", record.metadata.ttl_epoch_seconds}};
  }

  return encoded;
}

nlohmann::json SendTransaction(const RemoteTarget& target, const nlohmann::json& request,
                               std::chrono::milliseconds timeout) {
  if (!request.is_object()) {
    throw std::invalid_argument("request must be a JSON object");
  }
  if (!request.contains("txn_id")) {
    throw std::invalid_argument("request must include txn_id");
  }
  if (!request["txn_id"].is_number_integer()) {
    throw std::invalid_argument("txn_id must be an integer");
  }
  const auto txn_id_value = request["txn_id"].get<std::int64_t>();
  if (txn_id_value < 0) {
    throw std::invalid_argument("txn_id must be within 0..2^63-1");
  }
  const auto txn_id_unsigned = static_cast<std::uint64_t>(txn_id_value);
  if (txn_id_unsigned > kMaxTxnId) {
    throw std::invalid_argument("txn_id must be within 0..2^63-1");
  }
  if (!request.contains("operations") || !request["operations"].is_array() ||
      request["operations"].empty()) {
    throw std::invalid_argument("operations array must be present and non-empty");
  }

  const std::string body = request.dump();
  if (body.empty()) {
    throw std::invalid_argument("request body is empty");
  }

  if (body.size() > kMaxFrameBytes) {
    throw std::invalid_argument("request exceeds maximum frame size");
  }

  const auto length = static_cast<std::uint32_t>(body.size());
  const std::uint32_t network_length = htonl(length);
  std::string frame;
  frame.reserve(sizeof(network_length) + body.size());
  frame.append(reinterpret_cast<const char*>(&network_length), sizeof(network_length));
  frame.append(body);

  const int socket_fd = OpenSocket(target, timeout);
  struct FdDeleter {
    void operator()(const int* fd_ptr) const {
      if (fd_ptr != nullptr && *fd_ptr >= 0) {
        close(*fd_ptr);
      }
      delete fd_ptr;
    }
  };
  const auto closer = std::unique_ptr<const int, FdDeleter>(new int(socket_fd));

  WriteAll(*closer, frame);

  std::string length_prefix = ReadExact(*closer, sizeof(std::uint32_t));
  if (length_prefix.size() != sizeof(std::uint32_t)) {
    throw std::runtime_error("connection closed before length prefix was received");
  }

  std::uint32_t payload_size = 0U;
  std::memcpy(&payload_size, length_prefix.data(), sizeof(payload_size));
  payload_size = ntohl(payload_size);

  if (payload_size == 0U || payload_size > kMaxFrameBytes) {
    throw std::runtime_error("received invalid frame length");
  }

  std::string payload = ReadExact(*closer, payload_size);
  if (payload.size() != payload_size) {
    throw std::runtime_error("connection closed before full frame was received");
  }

  const auto response = ParseJson(payload);
  if (!response.is_object()) {
    throw std::runtime_error("response payload must be a JSON object");
  }

  if (response.contains("txn_id") && request["txn_id"].is_number_integer()) {
    if (response["txn_id"] != request["txn_id"]) {
      throw std::runtime_error("response txn_id does not match request");
    }
  }

  return response;
}

} // namespace jubilant::cli
