#include "storage/btree/btree.h"
#include "storage/simple_store.h"

#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
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

void PrintUsage() {
  std::cout << "jubectl <command> [args]\n"
            << "Commands:\n"
            << "  init <db_dir>\n"
            << "  set <db_dir> <key> <type> <value>\n"
            << "  get <db_dir> <key>\n"
            << "  del <db_dir> <key>\n"
            << "  stats <db_dir>\n"
            << "  validate <db_dir>\n";
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
    if (argc < 2) {
      PrintUsage();
      return EXIT_FAILURE;
    }

    const std::string command{argv[1]};
    if (command == "init") {
      if (argc != 3) {
        PrintUsage();
        return EXIT_FAILURE;
      }
      return HandleInit(argv[2]);
    }

    if (command == "set") {
      if (argc != 6) {
        PrintUsage();
        return EXIT_FAILURE;
      }
      return HandleSet(
          {.db_dir = argv[2], .key = argv[3], .record_args = {.type = argv[4], .value = argv[5]}});
    }

    if (command == "get") {
      if (argc != 4) {
        PrintUsage();
        return EXIT_FAILURE;
      }
      return HandleGet({.db_dir = argv[2], .key = argv[3]});
    }

    if (command == "del") {
      if (argc != 4) {
        PrintUsage();
        return EXIT_FAILURE;
      }
      return HandleDel({.db_dir = argv[2], .key = argv[3]});
    }

    if (command == "stats") {
      if (argc != 3) {
        PrintUsage();
        return EXIT_FAILURE;
      }
      return HandleStats(argv[2]);
    }

    if (command == "validate") {
      if (argc != 3) {
        PrintUsage();
        return EXIT_FAILURE;
      }
      return HandleValidate(argv[2]);
    }

    std::cerr << "Command '" << command << "' not yet implemented.\n";
    PrintUsage();
    return EXIT_FAILURE;
  } catch (const std::exception& ex) {
    std::cerr << "Error: " << ex.what() << "\n";
    return EXIT_FAILURE;
  }
}
