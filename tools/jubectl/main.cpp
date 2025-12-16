#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "storage/btree/btree.h"
#include "storage/simple_store.h"

namespace {

void PrintUsage() {
  std::cout << "jubectl <command> [args]\n"
            << "Commands:\n"
            << "  init <db_dir>\n"
            << "  set <db_dir> <key> <type> <value>\n"
            << "  get <db_dir> <key>\n"
            << "  del <db_dir> <key>\n";
}

std::vector<std::byte> ParseHex(std::string_view hex) {
  if (hex.size() % 2 != 0) {
    throw std::invalid_argument("Hex input must have even length");
  }

  std::vector<std::byte> data;
  data.reserve(hex.size() / 2);

  for (std::size_t i = 0; i < hex.size(); i += 2) {
    const auto byte_str = hex.substr(i, 2);
    auto hex_val = [](char c) -> unsigned {
      if (c >= '0' && c <= '9') {
        return static_cast<unsigned>(c - '0');
      }
      if (c >= 'a' && c <= 'f') {
        return static_cast<unsigned>(10 + (c - 'a'));
      }
      if (c >= 'A' && c <= 'F') {
        return static_cast<unsigned>(10 + (c - 'A'));
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

jubilant::storage::btree::Record BuildRecord(std::string_view type,
                                            std::string_view value) {
  jubilant::storage::btree::Record record{};

  if (type == "bytes") {
    record.value = ParseHex(value);
  } else if (type == "string") {
    record.value = std::string{value};
  } else if (type == "int") {
    record.value = std::stoll(std::string{value});
  } else {
    throw std::invalid_argument("Unknown value type");
  }

  return record;
}

int HandleSet(std::string_view db_dir, std::string_view key,
              std::string_view type, std::string_view value) {
  auto store = jubilant::storage::SimpleStore::Open(db_dir);
  const auto record = BuildRecord(type, value);
  store.Set(std::string{key}, record);
  store.Sync();
  std::cout << "OK\n";
  return EXIT_SUCCESS;
}

int HandleGet(std::string_view db_dir, std::string_view key) {
  auto store = jubilant::storage::SimpleStore::Open(db_dir);
  const auto result = store.Get(std::string{key});
  if (!result.has_value()) {
    std::cout << "(nil)\n";
    return EXIT_SUCCESS;
  }

  const auto& value = result->value;
  if (std::holds_alternative<std::vector<std::byte>>(value)) {
    const auto& bytes = std::get<std::vector<std::byte>>(value);
    std::cout << "bytes:";
    for (const auto b : bytes) {
      std::cout << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                << static_cast<int>(b);
    }
    std::cout << std::dec << "\n";
  } else if (std::holds_alternative<std::string>(value)) {
    std::cout << "string:" << std::get<std::string>(value) << "\n";
  } else {
    std::cout << "int:" << std::get<std::int64_t>(value) << "\n";
  }
  return EXIT_SUCCESS;
}

int HandleDel(std::string_view db_dir, std::string_view key) {
  auto store = jubilant::storage::SimpleStore::Open(db_dir);
  const bool removed = store.Delete(std::string{key});
  store.Sync();
  std::cout << (removed ? "(1)\n" : "(0)\n");
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
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
  } else if (command == "set") {
    if (argc != 6) {
      PrintUsage();
      return EXIT_FAILURE;
    }
    return HandleSet(argv[2], argv[3], argv[4], argv[5]);
  } else if (command == "get") {
    if (argc != 4) {
      PrintUsage();
      return EXIT_FAILURE;
    }
    return HandleGet(argv[2], argv[3]);
  } else if (command == "del") {
    if (argc != 4) {
      PrintUsage();
      return EXIT_FAILURE;
    }
    return HandleDel(argv[2], argv[3]);
  }

  std::cerr << "Command '" << command << "' not yet implemented.\n";
  PrintUsage();
  return EXIT_FAILURE;
}
