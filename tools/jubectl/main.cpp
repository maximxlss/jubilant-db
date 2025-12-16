// Copyright 2024 Jubilant DB

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "pager.h"

namespace {

void PrintUsage() {
  std::cout << "jubectl <command> [args]\n"
            << "Commands:\n"
            << "  init <db_dir>\n"
            << "  set <db_dir> <key> <type> <value>\n"
            << "  get <db_dir> <key>\n"
            << "  del <db_dir> <key>\n";
}

int HandleInit(std::string_view db_dir) {
  const std::filesystem::path data_path =
      std::filesystem::path(db_dir) / "data.pages";
  auto pager =
      jubilant::storage::Pager::Open(data_path, jubilant::storage::kDefaultPageSize);
  pager.Sync();
  std::cout << "Initialized scaffolding at " << data_path << "\n";
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
  }

  std::cerr << "Command '" << command << "' not yet implemented.\n";
  PrintUsage();
  return EXIT_FAILURE;
}
