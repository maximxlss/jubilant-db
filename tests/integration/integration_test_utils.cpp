#include "integration_test_utils.h"

#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <netinet/in.h>
#include <poll.h>
#include <random>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace jubilant::test {
namespace {

std::filesystem::path SourceRoot() {
  auto cursor = std::filesystem::absolute(std::filesystem::path(__FILE__));
  cursor = cursor.parent_path();
  while (!cursor.empty() && !std::filesystem::exists(cursor / "CMakeLists.txt")) {
    cursor = cursor.parent_path();
  }
  return cursor;
}

std::filesystem::path FindExistingPath(const std::vector<std::filesystem::path>& candidates) {
  for (const auto& candidate : candidates) {
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

std::string SafeReadFile(const std::filesystem::path& path) {
  std::ifstream stream(path);
  if (!stream) {
    return {};
  }

  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::optional<std::uint16_t> ParsePortFromLog(const std::string& log_contents) {
  static const std::regex kPattern{R"(jubildb server started with [0-9]+ workers at .*:([0-9]+))"};
  std::smatch match;
  if (std::regex_search(log_contents, match, kPattern) && match.size() >= 2) {
    try {
      const auto parsed = std::stoul(match[1]);
      if (parsed > 0U && parsed <= std::numeric_limits<std::uint16_t>::max()) {
        return static_cast<std::uint16_t>(parsed);
      }
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }
  return std::nullopt;
}

} // namespace

std::filesystem::path FindJubectlBinary() {
  const auto cwd = std::filesystem::current_path();
  const auto root = SourceRoot();
  return FindExistingPath(
      {cwd / "jubectl", root / "build/dev-debug/jubectl", root / "build/dev-debug-tidy/jubectl"});
}

std::filesystem::path FindPythonClientDir() {
  const auto cwd = std::filesystem::current_path();
  const auto root = SourceRoot();
  return FindExistingPath({cwd / "python_clients", root / "tools/clients/python"});
}

std::filesystem::path FindServerBinary() {
  const auto cwd = std::filesystem::current_path();
  const auto root = SourceRoot();
  return FindExistingPath({cwd / "jubildb_server", root / "build/dev-debug/jubildb_server",
                           root / "build/dev-debug-tidy/jubildb_server"});
}

std::string PythonExecutable() {
  if (std::filesystem::exists("/usr/bin/python3")) {
    return "/usr/bin/python3";
  }
  if (std::filesystem::exists("/usr/local/bin/python3")) {
    return "/usr/local/bin/python3";
  }
  return "python3";
}

CommandResult RunCommand(const std::vector<std::string>& args,
                         const std::filesystem::path& working_dir,
                         const std::vector<std::pair<std::string, std::string>>& env,
                         std::chrono::milliseconds timeout) {
  if (args.empty()) {
    throw std::invalid_argument("args must not be empty");
  }

  int pipe_fds[2]{-1, -1};
  if (pipe(pipe_fds) != 0) {
    throw std::runtime_error("Failed to create pipe for subprocess");
  }

  const pid_t pid = fork();
  if (pid < 0) {
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    throw std::runtime_error("fork failed");
  }

  if (pid == 0) {
    if (!working_dir.empty()) {
      if (chdir(working_dir.c_str()) != 0) {
        _exit(127);
      }
    }
    for (const auto& [key, value] : env) {
      if (setenv(key.c_str(), value.c_str(), 1) != 0) {
        _exit(127);
      }
    }

    if (dup2(pipe_fds[1], STDOUT_FILENO) == -1 || dup2(pipe_fds[1], STDERR_FILENO) == -1) {
      _exit(127);
    }
    close(pipe_fds[0]);
    close(pipe_fds[1]);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    execvp(argv.front(), argv.data());
    _exit(127);
  }

  close(pipe_fds[1]);
  const int current_flags = fcntl(pipe_fds[0], F_GETFL);
  if (current_flags == -1 || fcntl(pipe_fds[0], F_SETFL, current_flags | O_NONBLOCK) == -1) {
    close(pipe_fds[0]);
    kill(pid, SIGKILL);
    int ignore_status = 0;
    waitpid(pid, &ignore_status, 0);
    throw std::runtime_error("Failed to set non-blocking mode for pipe");
  }

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  bool timed_out = false;
  std::string output;
  std::array<char, 4096> buffer{};
  int status = 0;
  while (true) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline && !timed_out) {
      timed_out = true;
      kill(pid, SIGKILL);
    }

    pollfd poll_fd{};
    poll_fd.fd = pipe_fds[0];
    poll_fd.events = POLLIN;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    const int wait_ms =
        timed_out
            ? 0
            : static_cast<int>(std::max<std::chrono::milliseconds::rep>(remaining.count(), 0LL));
    const int ready = poll(&poll_fd, 1, wait_ms);
    if (ready > 0 && (poll_fd.revents & POLLIN) != 0) {
      while (true) {
        const auto bytes_read = read(pipe_fds[0], buffer.data(), buffer.size());
        if (bytes_read > 0) {
          output.append(buffer.data(), static_cast<std::size_t>(bytes_read));
          continue;
        }
        if (bytes_read == -1 && errno == EINTR) {
          continue;
        }
        break;
      }
    }

    const auto waited = waitpid(pid, &status, timed_out ? 0 : WNOHANG);
    if (waited == pid) {
      while (true) {
        const auto bytes_read = read(pipe_fds[0], buffer.data(), buffer.size());
        if (bytes_read > 0) {
          output.append(buffer.data(), static_cast<std::size_t>(bytes_read));
          continue;
        }
        if (bytes_read == -1 && errno == EINTR) {
          continue;
        }
        break;
      }
      break;
    }
    if (waited == -1 && errno != EINTR) {
      close(pipe_fds[0]);
      throw std::runtime_error("waitpid failed");
    }
  }
  close(pipe_fds[0]);

  CommandResult result{};
  result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  result.timed_out = timed_out;
  result.output = std::move(output);
  return result;
}

CommandResult RunPythonClientCommand(const std::filesystem::path& client_dir,
                                     const std::vector<std::string>& extra_args,
                                     std::uint16_t port) {
  const auto executable = PythonExecutable();
  const auto script = client_dir / "jubectl_client.py";

  std::vector<std::string> args{executable,  script.string(), "--host",
                                "127.0.0.1", "--port",        std::to_string(port)};
  args.insert(args.end(), extra_args.begin(), extra_args.end());

  return RunCommand(args, client_dir,
                    {{"PYTHONPATH", client_dir.string()}, {"PYTHONUNBUFFERED", "1"}});
}

CommandResult RunJubectlRemoteCommand(const std::filesystem::path& binary,
                                      const std::vector<std::string>& extra_args,
                                      std::uint16_t port, std::chrono::milliseconds timeout) {
  std::vector<std::string> args{binary.string(), "--remote", "127.0.0.1:" + std::to_string(port),
                                "--timeout-ms", std::to_string(timeout.count())};
  args.insert(args.end(), extra_args.begin(), extra_args.end());
  return RunCommand(args, std::filesystem::current_path(), {}, timeout + std::chrono::seconds(1));
}

nlohmann::json ParseJson(const std::string& payload) {
  const auto parsed = nlohmann::json::parse(payload, nullptr, false);
  if (parsed.is_discarded()) {
    throw std::runtime_error("Failed to parse JSON payload: " + payload);
  }
  return parsed;
}

nlohmann::json RunPythonClient(const std::filesystem::path& client_dir, std::string_view command,
                               std::string_view key, const std::string& value,
                               std::string_view value_type, std::uint16_t port) {
  std::vector<std::string> args{std::string{command}, std::string{key}};
  if (!value_type.empty()) {
    args.emplace_back(value_type);
    args.emplace_back(value);
  }

  const auto result = RunPythonClientCommand(client_dir, args, port);
  if (result.exit_code != 0) {
    throw std::runtime_error(std::string{"Python client failed"} +
                             (result.timed_out ? " (timed out)" : "") + ": " + result.output);
  }
  return ParseJson(result.output);
}

nlohmann::json RunJubectlRemote(const std::filesystem::path& binary, std::string_view command,
                                std::string_view key, const std::string& value,
                                std::string_view value_type, std::uint16_t port,
                                std::chrono::milliseconds timeout) {
  std::vector<std::string> args{std::string{command}, std::string{key}};
  if (!value_type.empty()) {
    args.emplace_back(value_type);
    args.emplace_back(value);
  }

  const auto result = RunJubectlRemoteCommand(binary, args, port, timeout);
  if (result.exit_code != 0) {
    throw std::runtime_error(std::string{"jubectl failed"} +
                             (result.timed_out ? " (timed out)" : "") + ": " + result.output);
  }
  return ParseJson(result.output);
}

CommandResult SendRawFrameToServer(std::uint16_t port, std::string_view payload) {
  CommandResult result{};

  const int socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd < 0) {
    result.exit_code = errno;
    result.output = "socket failed";
    return result;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (::connect(socket_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    result.exit_code = errno;
    result.output = "connect failed";
    ::close(socket_fd);
    return result;
  }

  const auto payload_length = static_cast<std::uint32_t>(payload.size());
  std::array<std::byte, 4> prefix{};
  const auto network_length = htonl(payload_length);
  std::memcpy(prefix.data(), &network_length, sizeof(network_length));

  if (::send(socket_fd, prefix.data(), prefix.size(), 0) < 0) {
    result.exit_code = errno;
    result.output = "send failed";
    ::close(socket_fd);
    return result;
  }

  if (!payload.empty()) {
    if (::send(socket_fd, payload.data(), payload.size(), 0) < 0) {
      result.exit_code = errno;
      result.output = "send payload failed";
      ::close(socket_fd);
      return result;
    }
  }

  std::array<char, 4096> buffer{};
  const auto received = ::recv(socket_fd, buffer.data(), buffer.size(), 0);
  if (received > 0) {
    result.output.assign(buffer.data(), static_cast<std::size_t>(received));
  }

  ::shutdown(socket_fd, SHUT_RDWR);
  ::close(socket_fd);
  return result;
}

bool ServerProcess::running() const noexcept {
  return pid > 0;
}

ServerProcess StartServerProcess(const ServerProcessConfig& config) {
  if (config.binary.empty() || !std::filesystem::exists(config.binary)) {
    throw std::invalid_argument("server binary missing");
  }

  ServerProcess process{};
  process.workspace = config.workspace.empty()
                          ? std::filesystem::temp_directory_path() /
                                ("jubildb-server-" + std::to_string(std::random_device{}()))
                          : config.workspace;
  std::filesystem::create_directories(process.workspace);

  process.db_path = config.db_path.empty() ? process.workspace / "db" : config.db_path;
  std::filesystem::create_directories(process.db_path);

  process.config_path = process.workspace / "server.toml";
  process.log_path = process.workspace / "jubildb_server.log";

  {
    std::ofstream config_stream(process.config_path, std::ios::trunc);
    if (!config_stream) {
      throw std::runtime_error("Failed to write server config at " + process.config_path.string());
    }
    config_stream << "db_path = \"" << process.db_path.string() << "\"\n";
    config_stream << "listen_address = \"" << config.host << "\"\n";
    config_stream << "listen_port = " << config.port << "\n";
  }

  const int log_fd = ::open(process.log_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (log_fd < 0) {
    throw std::runtime_error("Failed to open server log at " + process.log_path.string());
  }

  const pid_t pid = fork();
  if (pid < 0) {
    ::close(log_fd);
    throw std::runtime_error("fork failed while starting server");
  }

  if (pid == 0) {
    const auto binary_dir = config.binary.parent_path();
    if (!binary_dir.empty()) {
      (void)chdir(binary_dir.c_str());
    } else if (!process.workspace.empty()) {
      (void)chdir(process.workspace.c_str());
    }
    if (dup2(log_fd, STDOUT_FILENO) < 0) {
      ::close(log_fd);
      _exit(127);
    }
    if (dup2(log_fd, STDERR_FILENO) < 0) {
      ::close(log_fd);
      _exit(127);
    }
    ::close(log_fd);

    std::vector<std::string> args{
        config.binary.string(),         "--config",  process.config_path.string(),  "--workers",
        std::to_string(config.workers), "--backlog", std::to_string(config.backlog)};

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& arg : args) {
      argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    execvp(argv.front(), argv.data());
    _exit(127);
  }

  ::close(log_fd);
  process.pid = pid;

  const auto deadline = std::chrono::steady_clock::now() + config.startup_timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    process.startup_log = SafeReadFile(process.log_path);
    if (const auto port = ParsePortFromLog(process.startup_log)) {
      process.port = *port;
      return process;
    }

    int status = 0;
    const auto waited = waitpid(pid, &status, WNOHANG);
    if (waited == pid) {
      throw std::runtime_error("jubildb_server exited early. Log:\n" + process.startup_log);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  kill(pid, SIGKILL);
  waitpid(pid, nullptr, 0);
  throw std::runtime_error("Timed out waiting for jubildb_server startup. Log:\n" +
                           SafeReadFile(process.log_path));
}

void StopServerProcess(ServerProcess& process, std::chrono::milliseconds timeout) {
  if (process.pid <= 0) {
    return;
  }

  kill(process.pid, SIGTERM);

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    int status = 0;
    const auto waited = waitpid(process.pid, &status, WNOHANG);
    if (waited == process.pid) {
      process.pid = -1;
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  kill(process.pid, SIGKILL);
  waitpid(process.pid, nullptr, 0);
  process.pid = -1;
}

} // namespace jubilant::test
