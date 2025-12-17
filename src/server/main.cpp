#include "config/config.h"
#include "server/network_server.h"
#include "server/server.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_should_stop{false};

void SignalHandler(int /*signal*/) {
  g_should_stop.store(true);
}

struct CliOptions {
  std::filesystem::path config_path;
  std::size_t workers{0};
  int backlog{16};
};

void PrintUsage(const char* binary) {
  std::cerr << "Usage: " << binary
            << " --config <path> [--workers <count>] [--backlog <pending_connections>]\n";
}

struct ParseResult {
  std::optional<CliOptions> options;
  bool help_requested{false};
};

ParseResult ParseArgs(int argc, char** argv) {
  ParseResult result{};
  CliOptions options{};

  for (int i = 1; i < argc; ++i) {
    const std::string arg{argv[i]};
    if (arg == "--help" || arg == "-h") {
      PrintUsage(argv[0]);
      result.help_requested = true;
      return result;
    }

    if (arg == "--config" && i + 1 < argc) {
      options.config_path = argv[++i];
    } else if (arg == "--workers" && i + 1 < argc) {
      try {
        options.workers = static_cast<std::size_t>(std::stoul(argv[++i]));
      } catch (const std::exception&) {
        PrintUsage(argv[0]);
        return result;
      }
    } else if (arg == "--backlog" && i + 1 < argc) {
      try {
        options.backlog = std::stoi(argv[++i]);
      } catch (const std::exception&) {
        PrintUsage(argv[0]);
        return result;
      }
    } else {
      PrintUsage(argv[0]);
      return result;
    }
  }

  if (options.config_path.empty()) {
    PrintUsage(argv[0]);
    return result;
  }

  if (options.backlog <= 0) {
    options.backlog = 1;
  }

  result.options = options;
  return result;
}

std::size_t ResolveWorkerCount(std::size_t requested) {
  if (requested > 0) {
    return requested;
  }
  const auto hardware = std::thread::hardware_concurrency();
  return hardware > 0 ? static_cast<std::size_t>(hardware) : 1U;
}

} // namespace

int main(int argc, char** argv) {
  try {
    const auto parse_result = ParseArgs(argc, argv);
    if (parse_result.help_requested) {
      return 0;
    }
    if (!parse_result.options.has_value()) {
      return 1;
    }
    const auto& options = parse_result.options;

    const auto config = jubilant::config::ConfigLoader::LoadFromFile(options->config_path);
    if (!config.has_value()) {
      std::cerr << "Failed to load configuration from " << options->config_path << "\n";
      return 1;
    }

    const auto worker_count = ResolveWorkerCount(options->workers);
    jubilant::server::Server core_server(*config, worker_count);
    core_server.Start();

    jubilant::server::NetworkServer::Config network_config{};
    network_config.host = config->listen_address;
    network_config.port = config->listen_port;
    network_config.backlog = options->backlog;

    jubilant::server::NetworkServer network_server(core_server, network_config);
    if (!network_server.Start()) {
      std::cerr << "Failed to start network adapter on " << network_config.host << ":"
                << network_config.port << "\n";
      core_server.Stop();
      return 1;
    }

    std::cout << "jubildb server started with " << worker_count << " workers at "
              << network_config.host << ":" << network_server.port() << "\n";
    std::cout << "Database path: " << std::filesystem::absolute(config->db_path).string() << "\n";

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    while (!g_should_stop.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    network_server.Stop();
    core_server.Stop();
    std::cout << "jubildb server shut down gracefully\n";
  } catch (const std::exception& ex) {
    std::cerr << "Server bootstrap failed: " << ex.what() << "\n";
    return 1;
  }
  return 0;
}
