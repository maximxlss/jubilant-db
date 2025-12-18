#include "integration_test_utils.h"

#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <random>
#include <string>
#include <system_error>

using jubilant::test::FindJubectlBinary;
using jubilant::test::FindServerBinary;
using jubilant::test::RunJubectlRemote;
using jubilant::test::ServerProcess;
using jubilant::test::ServerProcessConfig;
using jubilant::test::StartServerProcess;
using jubilant::test::StopServerProcess;

namespace {

class ServerProcessIntegrationTest : public ::testing::Test {
protected:
  struct ServerStartupOptions {
    std::uint16_t port;
    int backlog;
  };

  void SetUp() override {
    server_binary_ = FindServerBinary();
    ASSERT_FALSE(server_binary_.empty()) << "jubildb_server binary not found";
    jubectl_binary_ = FindJubectlBinary();
    ASSERT_FALSE(jubectl_binary_.empty()) << "jubectl binary not found";

    const auto suffix = std::random_device{}();
    workspace_ = std::filesystem::temp_directory_path() /
                 ("jubildb-server-process-" + std::to_string(suffix));
    db_path_ = workspace_ / "db";
  }

  void TearDown() override {
    StopServerProcess(server_, std::chrono::milliseconds{2000});
    if (!workspace_.empty()) {
      std::error_code error_code;
      std::filesystem::remove_all(workspace_, error_code);
    }
  }

  void StartServer(const ServerStartupOptions& options) {
    ServerProcessConfig config{};
    config.binary = server_binary_;
    config.workspace = workspace_;
    config.db_path = db_path_;
    config.port = options.port;
    config.backlog = options.backlog;
    config.workers = 2;
    config.startup_timeout = std::chrono::seconds{8};
    server_ = StartServerProcess(config);
  }

  std::filesystem::path server_binary_;
  std::filesystem::path jubectl_binary_;
  std::filesystem::path workspace_;
  std::filesystem::path db_path_;
  ServerProcess server_{};
};

TEST_F(ServerProcessIntegrationTest, RemoteWritesSurviveRestartAndCoverValueKinds) {
  StartServer(ServerStartupOptions{.port = 0, .backlog = 8});
  ASSERT_TRUE(server_.running());
  ASSERT_GT(server_.port, 0);

  const std::string string_key = "server-restart-string";
  const std::string bytes_key = "server-restart-bytes";
  const std::string int_key = "server-restart-int";

  const auto set_string = RunJubectlRemote(jubectl_binary_, "set", string_key, "persisted-value",
                                           "string", server_.port);
  ASSERT_EQ(set_string.at("state"), "committed");
  ASSERT_TRUE(set_string.at("operations")[0].at("success").get<bool>());

  const auto set_bytes =
      RunJubectlRemote(jubectl_binary_, "set", bytes_key, "DEADBEEF", "bytes", server_.port);
  ASSERT_EQ(set_bytes.at("state"), "committed");

  const auto set_int =
      RunJubectlRemote(jubectl_binary_, "set", int_key, "915", "int", server_.port);
  ASSERT_EQ(set_int.at("state"), "committed");

  StopServerProcess(server_, std::chrono::milliseconds{2000});
  ASSERT_FALSE(server_.running());

  StartServer(ServerStartupOptions{.port = 0, .backlog = 4});
  ASSERT_TRUE(server_.running());
  ASSERT_GT(server_.port, 0);

  const auto get_string = RunJubectlRemote(jubectl_binary_, "get", string_key, "", "", server_.port,
                                           std::chrono::milliseconds{3000});
  ASSERT_EQ(get_string.at("state"), "committed");
  const auto& string_op = get_string.at("operations")[0];
  ASSERT_TRUE(string_op.at("success").get<bool>());
  ASSERT_TRUE(string_op.contains("value"));
  EXPECT_EQ(string_op.at("value").at("kind"), "string");
  EXPECT_EQ(string_op.at("value").at("data"), "persisted-value");

  const auto get_bytes = RunJubectlRemote(jubectl_binary_, "get", bytes_key, "", "", server_.port,
                                          std::chrono::milliseconds{3000});
  ASSERT_EQ(get_bytes.at("state"), "committed");
  const auto& bytes_op = get_bytes.at("operations")[0];
  ASSERT_TRUE(bytes_op.at("success").get<bool>());
  ASSERT_TRUE(bytes_op.contains("value"));
  EXPECT_EQ(bytes_op.at("value").at("kind"), "bytes");
  EXPECT_EQ(bytes_op.at("value").at("data"), "3q2+7w==");

  const auto get_int = RunJubectlRemote(jubectl_binary_, "get", int_key, "", "", server_.port,
                                        std::chrono::milliseconds{3000});
  ASSERT_EQ(get_int.at("state"), "committed");
  const auto& int_op = get_int.at("operations")[0];
  ASSERT_TRUE(int_op.at("success").get<bool>());
  ASSERT_TRUE(int_op.contains("value"));
  EXPECT_EQ(int_op.at("value").at("kind"), "int");
  EXPECT_EQ(int_op.at("value").at("data"), 915);

  const auto delete_string =
      RunJubectlRemote(jubectl_binary_, "del", string_key, "", "", server_.port);
  ASSERT_EQ(delete_string.at("state"), "committed");
  ASSERT_TRUE(delete_string.at("operations")[0].at("success").get<bool>());

  const auto confirm_string_missing =
      RunJubectlRemote(jubectl_binary_, "get", string_key, "", "", server_.port);
  ASSERT_EQ(confirm_string_missing.at("state"), "committed");
  EXPECT_FALSE(confirm_string_missing.at("operations")[0].at("success").get<bool>());
}

} // namespace
