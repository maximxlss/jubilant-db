#include "server/network_server.h"
#include "server/server.h"
#include "storage/simple_store.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <variant>
#include <vector>

using jubilant::server::NetworkServer;
using jubilant::server::Server;

namespace {

struct CommandResult {
  int exit_code{0};
  std::string output;
};

struct IntegrationScenario {
  std::string name;
  bool delete_missing_first{false};
  bool use_large_value{false};
};

std::string ScenarioName(const testing::TestParamInfo<IntegrationScenario>& info) {
  return info.param.name;
}

std::filesystem::path SourceRoot() {
  std::filesystem::path cursor = std::filesystem::absolute(std::filesystem::path(__FILE__));
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
                         const std::vector<std::pair<std::string, std::string>>& env = {}) {
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

    if (argv.empty()) {
      _exit(127);
    }
    execvp(argv.front(), argv.data());
    _exit(127);
  }

  close(pipe_fds[1]);
  std::string output;
  std::array<char, 4096> buffer{};
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
  close(pipe_fds[0]);

  int status = 0;
  while (true) {
    const auto waited = waitpid(pid, &status, 0);
    if (waited == -1 && errno == EINTR) {
      continue;
    }
    if (waited == -1) {
      throw std::runtime_error("waitpid failed");
    }
    break;
  }

  CommandResult result{};
  result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  result.output = std::move(output);
  return result;
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
  const auto executable = PythonExecutable();
  const auto script = client_dir / "jubectl_client.py";
  std::vector<std::string> args{executable,           script.string(), "--host",
                                "127.0.0.1",          "--port",        std::to_string(port),
                                std::string{command}, std::string{key}};
  if (!value_type.empty()) {
    args.emplace_back(value_type);
    args.emplace_back(value);
  }

  const auto result = RunCommand(args, client_dir,
                                 {{"PYTHONPATH", client_dir.string()}, {"PYTHONUNBUFFERED", "1"}});
  if (result.exit_code != 0) {
    throw std::runtime_error("Python client failed: " + result.output);
  }
  return ParseJson(result.output);
}

nlohmann::json RunJubectlRemote(const std::filesystem::path& binary, std::string_view command,
                                std::string_view key, const std::string& value,
                                std::string_view value_type, std::uint16_t port) {
  std::vector<std::string> args{binary.string(), "--remote", "127.0.0.1:" + std::to_string(port),
                                "--timeout-ms",  "2000",     std::string{command},
                                std::string{key}};
  if (!value_type.empty()) {
    args.emplace_back(value_type);
    args.emplace_back(value);
  }

  const auto result = RunCommand(args, std::filesystem::current_path());
  if (result.exit_code != 0) {
    throw std::runtime_error("jubectl failed: " + result.output);
  }
  return ParseJson(result.output);
}

class DualClientIntegrationTest : public ::testing::TestWithParam<IntegrationScenario> {
protected:
  void SetUp() override {
    const auto suffix = std::random_device{}();
    temp_dir_ =
        std::filesystem::temp_directory_path() / ("jubildb-dual-client-" + std::to_string(suffix));
    std::filesystem::remove_all(temp_dir_);
    std::filesystem::create_directories(temp_dir_);

    core_server_ = std::make_unique<Server>(temp_dir_, 2);
    core_server_->Start();

    NetworkServer::Config config{};
    config.host = "127.0.0.1";
    config.port = 0;
    config.backlog = 16;
    network_server_ = std::make_unique<NetworkServer>(*core_server_, config);
    ASSERT_TRUE(network_server_->Start());
    ASSERT_GT(network_server_->port(), 0);
  }

  void TearDown() override {
    if (network_server_) {
      network_server_->Stop();
      network_server_.reset();
    }
    if (core_server_) {
      core_server_->Stop();
      core_server_.reset();
    }
    if (!temp_dir_.empty()) {
      std::filesystem::remove_all(temp_dir_);
    }
  }

  [[nodiscard]] std::uint16_t port() const {
    return network_server_ ? network_server_->port() : 0;
  }

  std::filesystem::path temp_dir_;
  std::unique_ptr<Server> core_server_;
  std::unique_ptr<NetworkServer> network_server_;
};

TEST_P(DualClientIntegrationTest, PythonAndJubectlStayConsistent) {
  const auto& scenario = GetParam();
  const auto client_dir = FindPythonClientDir();
  ASSERT_FALSE(client_dir.empty()) << "Python client bundle not found";
  const auto jubectl = FindJubectlBinary();
  ASSERT_FALSE(jubectl.empty()) << "jubectl binary not found";

  const std::string primary_key = "alpha-key-" + scenario.name;
  const std::string missing_key = "missing-" + scenario.name;

  const std::string initial_value =
      scenario.use_large_value ? std::string(120'000, 'x') : std::string{"initial-value"};
  const std::string overwrite_value =
      scenario.use_large_value ? std::string{"remote-overwrite"} : std::string{"secondary"};
  const std::string reinsertion_value =
      scenario.use_large_value ? std::string(4'096, 'y') : std::string{"final-value"};

  if (scenario.delete_missing_first) {
    const auto missing_delete = RunPythonClient(client_dir, "del", missing_key, "", "", port());
    ASSERT_EQ(missing_delete.at("state"), "committed");
    ASSERT_FALSE(missing_delete.at("operations")[0].at("success").get<bool>());
  }

  const auto python_set =
      RunPythonClient(client_dir, "set", primary_key, initial_value, "string", port());
  ASSERT_EQ(python_set.at("state"), "committed");
  ASSERT_TRUE(python_set.at("operations")[0].at("success").get<bool>());

  const auto python_get = RunPythonClient(client_dir, "get", primary_key, "", "", port());
  ASSERT_EQ(python_get.at("state"), "committed");
  const auto& python_get_op = python_get.at("operations")[0];
  ASSERT_TRUE(python_get_op.at("success").get<bool>());
  EXPECT_EQ(python_get_op.at("value").at("data").get<std::string>(), initial_value);

  const auto jubectl_set =
      RunJubectlRemote(jubectl, "set", primary_key, overwrite_value, "string", port());
  ASSERT_EQ(jubectl_set.at("state"), "committed");
  ASSERT_TRUE(jubectl_set.at("operations")[0].at("success").get<bool>());

  const auto jubectl_missing_delete = RunJubectlRemote(jubectl, "del", missing_key, "", "", port());
  ASSERT_EQ(jubectl_missing_delete.at("state"), "committed");
  ASSERT_FALSE(jubectl_missing_delete.at("operations")[0].at("success").get<bool>());

  const auto python_after_overwrite =
      RunPythonClient(client_dir, "get", primary_key, "", "", port());
  ASSERT_EQ(python_after_overwrite.at("state"), "committed");
  const auto& overwritten_op = python_after_overwrite.at("operations")[0];
  ASSERT_TRUE(overwritten_op.at("success").get<bool>());
  EXPECT_EQ(overwritten_op.at("value").at("data").get<std::string>(), overwrite_value);

  const auto jubectl_delete = RunJubectlRemote(jubectl, "del", primary_key, "", "", port());
  ASSERT_EQ(jubectl_delete.at("state"), "committed");
  ASSERT_TRUE(jubectl_delete.at("operations")[0].at("success").get<bool>());

  const auto python_after_delete = RunPythonClient(client_dir, "get", primary_key, "", "", port());
  ASSERT_EQ(python_after_delete.at("state"), "committed");
  EXPECT_FALSE(python_after_delete.at("operations")[0].at("success").get<bool>());

  const auto python_reinsert =
      RunPythonClient(client_dir, "set", primary_key, reinsertion_value, "string", port());
  ASSERT_EQ(python_reinsert.at("state"), "committed");
  ASSERT_TRUE(python_reinsert.at("operations")[0].at("success").get<bool>());

  const auto jubectl_final = RunJubectlRemote(jubectl, "get", primary_key, "", "", port());
  ASSERT_EQ(jubectl_final.at("state"), "committed");
  const auto& final_op = jubectl_final.at("operations")[0];
  ASSERT_TRUE(final_op.at("success").get<bool>());
  EXPECT_EQ(final_op.at("value").at("data").get<std::string>(), reinsertion_value);

  network_server_->Stop();
  core_server_->Stop();

  auto reloaded_store = jubilant::storage::SimpleStore::Open(temp_dir_);
  const auto reloaded_record = reloaded_store.Get(primary_key);
  if (!reloaded_record.has_value()) {
    FAIL() << "Reloaded store missing primary key";
    return;
  }
  const auto& stored_record = *reloaded_record;
  ASSERT_TRUE(std::holds_alternative<std::string>(stored_record.value));
  EXPECT_EQ(std::get<std::string>(stored_record.value), reinsertion_value);
  EXPECT_FALSE(reloaded_store.Get(missing_key).has_value());
  EXPECT_EQ(reloaded_store.size(), 1U);

  const auto pages_path = temp_dir_ / "data.pages";
  if (std::filesystem::exists(pages_path)) {
    EXPECT_GT(std::filesystem::file_size(pages_path), 0U);
  } else {
    ADD_FAILURE() << "data.pages missing under " << temp_dir_;
  }
  if (scenario.use_large_value) {
    const auto vlog_path = temp_dir_ / "vlog" / "segment-0.vlog";
    if (std::filesystem::exists(vlog_path)) {
      EXPECT_GT(std::filesystem::file_size(vlog_path), 0U);
    } else {
      ADD_FAILURE() << "Expected value log segment for large value scenario";
    }
  }
}

INSTANTIATE_TEST_SUITE_P(DualClient, DualClientIntegrationTest,
                         testing::Values(IntegrationScenario{.name = "SmallString",
                                                             .delete_missing_first = false},
                                         IntegrationScenario{.name = "LargeValueWithMissingDeletes",
                                                             .delete_missing_first = true,
                                                             .use_large_value = true}),
                         ScenarioName);

} // namespace
