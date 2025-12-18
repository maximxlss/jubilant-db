#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <utility>
#include <vector>

namespace jubilant::test {

struct CommandResult {
  int exit_code{0};
  bool timed_out{false};
  std::string output;
};

std::filesystem::path FindJubectlBinary();
std::filesystem::path FindPythonClientDir();
std::filesystem::path FindServerBinary();
std::string PythonExecutable();

CommandResult RunCommand(const std::vector<std::string>& args,
                         const std::filesystem::path& working_dir,
                         const std::vector<std::pair<std::string, std::string>>& env = {},
                         std::chrono::milliseconds timeout = std::chrono::milliseconds{5000});

CommandResult RunPythonClientCommand(const std::filesystem::path& client_dir,
                                     const std::vector<std::string>& extra_args,
                                     std::uint16_t port);
CommandResult RunJubectlRemoteCommand(const std::filesystem::path& binary,
                                      const std::vector<std::string>& extra_args,
                                      std::uint16_t port,
                                      std::chrono::milliseconds timeout =
                                          std::chrono::milliseconds{2000});

nlohmann::json ParseJson(const std::string& payload);
nlohmann::json RunPythonClient(const std::filesystem::path& client_dir, std::string_view command,
                               std::string_view key, const std::string& value,
                               std::string_view value_type, std::uint16_t port);
nlohmann::json RunJubectlRemote(const std::filesystem::path& binary, std::string_view command,
                                std::string_view key, const std::string& value,
                                std::string_view value_type, std::uint16_t port,
                                std::chrono::milliseconds timeout =
                                    std::chrono::milliseconds{2000});

CommandResult SendRawFrameToServer(std::uint16_t port, std::string_view payload);

struct ServerProcessConfig {
  std::filesystem::path binary;
  std::filesystem::path workspace;
  std::filesystem::path db_path;
  std::string host{"127.0.0.1"};
  std::uint16_t port{0};
  std::size_t workers{2};
  int backlog{16};
  std::chrono::milliseconds startup_timeout{std::chrono::seconds{5}};
};

struct ServerProcess {
  pid_t pid{-1};
  std::uint16_t port{0};
  std::filesystem::path workspace;
  std::filesystem::path db_path;
  std::filesystem::path config_path;
  std::filesystem::path log_path;
  std::string startup_log;

  [[nodiscard]] bool running() const noexcept;
};

ServerProcess StartServerProcess(const ServerProcessConfig& config);
void StopServerProcess(ServerProcess& process, std::chrono::milliseconds timeout);

} // namespace jubilant::test
