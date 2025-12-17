#include "remote_client.h"
#include "storage/btree/btree.h"
#include "storage/simple_store.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

struct RecordArgs {
  std::string_view type;
  std::string_view value;
};

struct SetCommand {
  std::string_view db_dir;
  std::string_view key;
  RecordArgs record_args;
};

struct GetCommand {
  std::string_view db_dir;
  std::string_view key;
};

struct DeleteCommand {
  std::string_view db_dir;
  std::string_view key;
};

struct RemoteOptions {
  bool enabled{false};
  jubilant::cli::RemoteTarget target{};
  std::optional<std::uint64_t> txn_id;
  std::chrono::milliseconds timeout{jubilant::cli::kDefaultRemoteTimeout};
};

struct ParsedArgs {
  RemoteOptions remote;
  std::vector<std::string_view> positionals;
};

void PrintUsage() {
  std::cout << "jubectl [--remote host:port] [--txn-id id] [--timeout-ms ms] <command> [args]\n"
            << "Local commands (default, on-disk store):\n"
            << "  init <db_dir>\n"
            << "  set <db_dir> <key> <bytes|string|int> <value>\n"
            << "  get <db_dir> <key>\n"
            << "  del <db_dir> <key>\n"
            << "  stats <db_dir>\n"
            << "  validate <db_dir>\n"
            << "\n"
            << "Remote commands (--remote required, speak txn-wire-v0.0.2):\n"
            << "  set <key> <bytes|string|int> <value>\n"
            << "  get <key>\n"
            << "  del <key>\n"
            << "  txn <request.json>  (JSON object or array of operations)\n";
}

std::vector<std::byte> ParseHex(std::string_view hex) {
  if (hex.size() % 2 != 0) {
    throw std::invalid_argument("Hex input must have even length");
  }

  std::vector<std::byte> data;
  data.reserve(hex.size() / 2);

  for (std::size_t i = 0; i < hex.size(); i += 2) {
    const auto byte_str = hex.substr(i, 2);
    auto hex_val = [](char hex_char) -> unsigned {
      if (hex_char >= '0' && hex_char <= '9') {
        return static_cast<unsigned>(hex_char - '0');
      }
      if (hex_char >= 'a' && hex_char <= 'f') {
        return static_cast<unsigned>(10 + (hex_char - 'a'));
      }
      if (hex_char >= 'A' && hex_char <= 'F') {
        return static_cast<unsigned>(10 + (hex_char - 'A'));
      }
      throw std::invalid_argument("Invalid hex digit");
    };

    const auto high = hex_val(byte_str[0]);
    const auto low = hex_val(byte_str[1]);
    data.push_back(static_cast<std::byte>((high << 4U) | low));
  }

  return data;
}

std::uint64_t ParseTxnIdArg(std::string_view value) {
  try {
    const auto parsed = std::stoull(std::string{value});
    if (parsed > jubilant::cli::kMaxTxnId) {
      throw std::invalid_argument("transaction id exceeds v0.0.2 maximum");
    }
    return parsed;
  } catch (const std::exception& ex) {
    throw std::invalid_argument(std::string{"Invalid --txn-id: "} + ex.what());
  }
}

std::chrono::milliseconds ParseTimeoutMs(std::string_view value) {
  try {
    const auto parsed = std::stoul(std::string{value});
    if (parsed == 0U) {
      throw std::invalid_argument("timeout must be positive");
    }
    return std::chrono::milliseconds{parsed};
  } catch (const std::exception& ex) {
    throw std::invalid_argument(std::string{"Invalid --timeout-ms: "} + ex.what());
  }
}

ParsedArgs ParseArguments(int argc, char** argv) {
  ParsedArgs parsed{};

  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--remote") {
      if (i + 1 >= argc) {
        throw std::invalid_argument("--remote requires host:port");
      }
      parsed.remote.enabled = true;
      parsed.remote.target = jubilant::cli::ParseRemoteTarget(argv[++i]);
      continue;
    }
    if (arg == "--txn-id") {
      if (i + 1 >= argc) {
        throw std::invalid_argument("--txn-id requires a value");
      }
      parsed.remote.txn_id = ParseTxnIdArg(argv[++i]);
      continue;
    }
    if (arg == "--timeout-ms") {
      if (i + 1 >= argc) {
        throw std::invalid_argument("--timeout-ms requires a value");
      }
      parsed.remote.timeout = ParseTimeoutMs(argv[++i]);
      continue;
    }

    parsed.positionals.assign(argv + i, argv + argc);
    break;
  }

  return parsed;
}

int HandleInit(std::string_view db_dir) {
  const auto store = jubilant::storage::SimpleStore::Open(db_dir);
  (void)store;
  std::cout << "Initialized DB at " << db_dir << "\n";
  return EXIT_SUCCESS;
}

jubilant::storage::btree::Record BuildRecord(const RecordArgs& args) {
  jubilant::storage::btree::Record record{};

  if (args.type == "bytes") {
    record.value = ParseHex(args.value);
  } else if (args.type == "string") {
    record.value = std::string{args.value};
  } else if (args.type == "int") {
    record.value = std::stoll(std::string{args.value});
  } else {
    throw std::invalid_argument("Unknown value type");
  }

  return record;
}

nlohmann::json BuildRemoteOperation(std::string_view type, std::string_view key,
                                    const RecordArgs* record_args) {
  if (type != "set" && type != "get" && type != "del") {
    throw std::invalid_argument("operation type must be set/get/del");
  }

  if (key.empty()) {
    throw std::invalid_argument("key must be non-empty");
  }

  nlohmann::json operation{{"type", type}, {"key", key}};
  if (type == "set") {
    if (record_args == nullptr) {
      throw std::invalid_argument("set operations require a value");
    }
    const auto record = BuildRecord(*record_args);
    operation["value"] = jubilant::cli::RecordValueToEnvelope(record);
  }
  return operation;
}

nlohmann::json BuildRemoteRequest(const RemoteOptions& remote,
                                  std::vector<nlohmann::json> operations) {
  if (operations.empty()) {
    throw std::invalid_argument("operations list must be non-empty");
  }

  nlohmann::json request;
  request["txn_id"] = remote.txn_id.value_or(jubilant::cli::GenerateTxnId());
  request["operations"] = std::move(operations);
  return request;
}

nlohmann::json LoadJsonFromFile(const std::filesystem::path& path) {
  std::ifstream stream(path);
  if (!stream) {
    throw std::runtime_error("Failed to open transaction file: " + path.string());
  }

  std::ostringstream buffer;
  buffer << stream.rdbuf();
  try {
    return nlohmann::json::parse(buffer.str());
  } catch (const nlohmann::json::parse_error& err) {
    throw std::runtime_error(std::string{"Invalid JSON in transaction file: "} + err.what());
  }
}

nlohmann::json NormalizeTransactionRequest(nlohmann::json request, const RemoteOptions& remote) {
  if (request.is_array()) {
    request = nlohmann::json{{"operations", request}};
  }

  if (!request.is_object()) {
    throw std::invalid_argument("transaction JSON must be an object or operations array");
  }

  if (!request.contains("operations") || !request["operations"].is_array() ||
      request["operations"].empty()) {
    throw std::invalid_argument("transaction JSON must include a non-empty operations array");
  }

  if (request.contains("txn_id") && !request["txn_id"].is_number_integer()) {
    throw std::invalid_argument("txn_id must be an integer when provided");
  }

  if (remote.txn_id.has_value()) {
    request["txn_id"] = *remote.txn_id;
  } else if (!request.contains("txn_id")) {
    request["txn_id"] = jubilant::cli::GenerateTxnId();
  }

  return request;
}

void PrintRemoteResponse(const nlohmann::json& response) {
  std::cout << response.dump(2) << "\n";
}

int HandleSet(SetCommand command) {
  auto store = jubilant::storage::SimpleStore::Open(command.db_dir);
  const auto record = BuildRecord(command.record_args);
  store.Set(std::string{command.key}, record);
  store.Sync();
  std::cout << "OK\n";
  return EXIT_SUCCESS;
}

int HandleGet(GetCommand command) {
  auto store = jubilant::storage::SimpleStore::Open(command.db_dir);
  const auto result = store.Get(std::string{command.key});
  if (!result.has_value()) {
    std::cout << "(nil)\n";
    return EXIT_SUCCESS;
  }

  const auto& value = result->value;
  if (std::holds_alternative<std::vector<std::byte>>(value)) {
    const auto& bytes = std::get<std::vector<std::byte>>(value);
    std::cout << "bytes:";
    for (const auto byte_value : bytes) {
      std::cout << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                << static_cast<int>(byte_value);
    }
    std::cout << std::dec << "\n";
  } else if (std::holds_alternative<std::string>(value)) {
    std::cout << "string:" << std::get<std::string>(value) << "\n";
  } else {
    std::cout << "int:" << std::get<std::int64_t>(value) << "\n";
  }
  return EXIT_SUCCESS;
}

int HandleDel(DeleteCommand command) {
  auto store = jubilant::storage::SimpleStore::Open(command.db_dir);
  const bool removed = store.Delete(std::string{command.key});
  store.Sync();
  std::cout << (removed ? "(1)\n" : "(0)\n");
  return EXIT_SUCCESS;
}

int HandleStats(std::string_view db_dir) {
  auto store = jubilant::storage::SimpleStore::Open(db_dir);
  const auto stats = store.stats();

  std::cout << "Manifest generation: " << stats.manifest.generation << "\n"
            << "Format: " << stats.manifest.format_major << '.' << stats.manifest.format_minor
            << "\n"
            << "Page size: " << stats.manifest.page_size
            << ", inline threshold: " << stats.manifest.inline_threshold << "\n"
            << "DB UUID: " << stats.manifest.db_uuid << "\n"
            << "Superblock generation: " << stats.superblock.generation << "\n"
            << "Root page id: " << stats.superblock.root_page_id << "\n"
            << "Last checkpoint LSN: " << stats.superblock.last_checkpoint_lsn << "\n"
            << "Page count: " << stats.page_count << "\n"
            << "Key count: " << stats.key_count << "\n";

  return EXIT_SUCCESS;
}

int HandleValidate(std::string_view db_dir) {
  const auto result = jubilant::storage::SimpleStore::ValidateOnDisk(db_dir);

  std::cout << "Manifest: " << (result.manifest_result.ok ? "OK" : "FAIL") << " - "
            << result.manifest_result.message << "\n";
  std::cout << "Superblock: " << (result.superblock_ok ? "OK" : "FAIL") << " - "
            << result.superblock_message << "\n";
  std::cout << "Checkpoint: " << (result.checkpoint_ok ? "OK" : "WARN") << " - "
            << result.checkpoint_message << "\n";

  return result.ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace

int main(int argc, char** argv) {
  try {
    const auto parsed = ParseArguments(argc, argv);
    if (parsed.positionals.empty()) {
      PrintUsage();
      return EXIT_FAILURE;
    }

    const std::string command{parsed.positionals[0]};

    const auto send_remote = [&](std::vector<nlohmann::json> operations) {
      const auto request = BuildRemoteRequest(parsed.remote, std::move(operations));
      const auto response =
          jubilant::cli::SendTransaction(parsed.remote.target, request, parsed.remote.timeout);
      PrintRemoteResponse(response);
      return EXIT_SUCCESS;
    };

    if (command == "init") {
      if (parsed.remote.enabled || parsed.positionals.size() != 2) {
        PrintUsage();
        return EXIT_FAILURE;
      }
      return HandleInit(parsed.positionals[1]);
    }

    if (command == "set") {
      if (parsed.remote.enabled) {
        if (parsed.positionals.size() != 4) {
          PrintUsage();
          return EXIT_FAILURE;
        }
        const RecordArgs record_args{.type = parsed.positionals[2], .value = parsed.positionals[3]};
        return send_remote({BuildRemoteOperation("set", parsed.positionals[1], &record_args)});
      }

      if (parsed.positionals.size() != 5) {
        PrintUsage();
        return EXIT_FAILURE;
      }
      return HandleSet(
          {.db_dir = parsed.positionals[1],
           .key = parsed.positionals[2],
           .record_args = {.type = parsed.positionals[3], .value = parsed.positionals[4]}});
    }

    if (command == "get") {
      if (parsed.remote.enabled) {
        if (parsed.positionals.size() != 2) {
          PrintUsage();
          return EXIT_FAILURE;
        }
        return send_remote({BuildRemoteOperation("get", parsed.positionals[1], nullptr)});
      }

      if (parsed.positionals.size() != 3) {
        PrintUsage();
        return EXIT_FAILURE;
      }
      return HandleGet({.db_dir = parsed.positionals[1], .key = parsed.positionals[2]});
    }

    if (command == "del") {
      if (parsed.remote.enabled) {
        if (parsed.positionals.size() != 2) {
          PrintUsage();
          return EXIT_FAILURE;
        }
        return send_remote({BuildRemoteOperation("del", parsed.positionals[1], nullptr)});
      }

      if (parsed.positionals.size() != 3) {
        PrintUsage();
        return EXIT_FAILURE;
      }
      return HandleDel({.db_dir = parsed.positionals[1], .key = parsed.positionals[2]});
    }

    if (command == "txn") {
      if (!parsed.remote.enabled || parsed.positionals.size() != 2) {
        PrintUsage();
        return EXIT_FAILURE;
      }

      auto request_json = LoadJsonFromFile(std::filesystem::path{parsed.positionals[1]});
      const auto normalized = NormalizeTransactionRequest(std::move(request_json), parsed.remote);
      const auto response =
          jubilant::cli::SendTransaction(parsed.remote.target, normalized, parsed.remote.timeout);
      PrintRemoteResponse(response);
      return EXIT_SUCCESS;
    }

    if (command == "stats") {
      if (parsed.remote.enabled || parsed.positionals.size() != 2) {
        PrintUsage();
        return EXIT_FAILURE;
      }
      return HandleStats(parsed.positionals[1]);
    }

    if (command == "validate") {
      if (parsed.remote.enabled || parsed.positionals.size() != 2) {
        PrintUsage();
        return EXIT_FAILURE;
      }
      return HandleValidate(parsed.positionals[1]);
    }

    std::cerr << "Command '" << command << "' not yet implemented.\n";
    PrintUsage();
    return EXIT_FAILURE;
  } catch (const std::exception& ex) {
    std::cerr << "Error: " << ex.what() << "\n";
    return EXIT_FAILURE;
  }
}
