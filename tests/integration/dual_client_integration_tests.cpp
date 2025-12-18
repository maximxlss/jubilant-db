#include "integration_test_utils.h"
#include "server/network_server.h"
#include "server/server.h"
#include "storage/simple_store.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using jubilant::server::NetworkServer;
using jubilant::server::Server;
using jubilant::test::CommandResult;
using jubilant::test::FindJubectlBinary;
using jubilant::test::FindPythonClientDir;
using jubilant::test::ParseJson;
using jubilant::test::RunJubectlRemote;
using jubilant::test::RunPythonClient;
using jubilant::test::RunPythonClientCommand;
using jubilant::test::SendRawFrameToServer;

namespace {

struct IntegrationScenario {
  std::string name;
  bool delete_missing_first{false};
  bool use_large_value{false};
};

enum class ClientVariant : std::uint8_t { kPython, kJubectl };

struct ClientEndpoints {
  std::filesystem::path python_client_dir;
  std::filesystem::path jubectl_binary;
};

struct ClientAction {
  ClientVariant client;
  std::string verb;
  std::string key;
  std::string value;
  std::string value_type;
  bool expect_success{true};
  std::optional<std::string> expect_value;
};

struct ActionMatrix {
  std::string name;
  std::string primary_key;
  std::vector<ClientAction> actions;
  std::optional<std::string> expected_terminal_value;
};

std::string ScenarioName(const testing::TestParamInfo<IntegrationScenario>& info) {
  return info.param.name;
}

nlohmann::json ExecuteClientAction(const ClientAction& action, const ClientEndpoints& endpoints,
                                   std::uint16_t port) {
  if (action.client == ClientVariant::kPython) {
    return RunPythonClient(endpoints.python_client_dir, action.verb, action.key, action.value,
                           action.value_type, port);
  }
  return RunJubectlRemote(endpoints.jubectl_binary, action.verb, action.key, action.value,
                          action.value_type, port);
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

TEST_F(DualClientIntegrationTest, InterleavedClientMatricesMaintainOrdering) {
  const auto client_dir = FindPythonClientDir();
  ASSERT_FALSE(client_dir.empty()) << "Python client bundle not found";
  const auto jubectl = FindJubectlBinary();
  ASSERT_FALSE(jubectl.empty()) << "jubectl binary not found";
  const ClientEndpoints endpoints{.python_client_dir = client_dir, .jubectl_binary = jubectl};

  const auto suffix = std::to_string(std::random_device{}());
  const std::vector<ActionMatrix> matrices{{.name = "DeleteThenReinsert",
                                            .primary_key = "matrix-delete-" + suffix,
                                            .actions = {{.client = ClientVariant::kPython,
                                                         .verb = "set",
                                                         .key = "matrix-delete-" + suffix,
                                                         .value = "python-writes",
                                                         .value_type = "string"},
                                                        {.client = ClientVariant::kJubectl,
                                                         .verb = "get",
                                                         .key = "matrix-delete-" + suffix,
                                                         .value_type = "",
                                                         .expect_value = "python-writes"},
                                                        {.client = ClientVariant::kJubectl,
                                                         .verb = "del",
                                                         .key = "matrix-delete-" + suffix,
                                                         .value_type = "",
                                                         .expect_success = true},
                                                        {.client = ClientVariant::kPython,
                                                         .verb = "get",
                                                         .key = "matrix-delete-" + suffix,
                                                         .value_type = "",
                                                         .expect_success = false},
                                                        {.client = ClientVariant::kPython,
                                                         .verb = "set",
                                                         .key = "matrix-delete-" + suffix,
                                                         .value = "reinserted-from-python",
                                                         .value_type = "string"},
                                                        {.client = ClientVariant::kJubectl,
                                                         .verb = "get",
                                                         .key = "matrix-delete-" + suffix,
                                                         .value_type = "",
                                                         .expect_value = "reinserted-from-python"}},
                                            .expected_terminal_value = "reinserted-from-python"},
                                           {.name = "OverwriteOrdering",
                                            .primary_key = "matrix-overwrite-" + suffix,
                                            .actions = {{.client = ClientVariant::kJubectl,
                                                         .verb = "set",
                                                         .key = "matrix-overwrite-" + suffix,
                                                         .value = "cli-first",
                                                         .value_type = "string"},
                                                        {.client = ClientVariant::kPython,
                                                         .verb = "set",
                                                         .key = "matrix-overwrite-" + suffix,
                                                         .value = "python-second",
                                                         .value_type = "string"},
                                                        {.client = ClientVariant::kJubectl,
                                                         .verb = "set",
                                                         .key = "matrix-overwrite-" + suffix,
                                                         .value = "cli-third",
                                                         .value_type = "string"},
                                                        {.client = ClientVariant::kPython,
                                                         .verb = "get",
                                                         .key = "matrix-overwrite-" + suffix,
                                                         .value_type = "",
                                                         .expect_value = "cli-third"}},
                                            .expected_terminal_value = "cli-third"}};

  for (const auto& matrix : matrices) {
    SCOPED_TRACE(matrix.name);
    for (const auto& action : matrix.actions) {
      const auto response = ExecuteClientAction(action, endpoints, port());
      ASSERT_EQ(response.at("state"), "committed");
      ASSERT_FALSE(response.at("operations").empty());
      const auto& operation_result = response.at("operations")[0];
      EXPECT_EQ(operation_result.at("success").get<bool>(), action.expect_success);
      if (action.expect_value.has_value()) {
        ASSERT_TRUE(operation_result.contains("value"));
        EXPECT_EQ(operation_result.at("value").at("data").get<std::string>(), *action.expect_value);
      } else if (action.verb == "get" && action.expect_success) {
        EXPECT_TRUE(operation_result.contains("value"));
      }
    }

    auto store = jubilant::storage::SimpleStore::Open(temp_dir_);
    const auto persisted = store.Get(matrix.primary_key);
    if (matrix.expected_terminal_value.has_value()) {
      if (!persisted.has_value()) {
        ADD_FAILURE() << "Expected key " << matrix.primary_key << " to exist for " << matrix.name;
      } else {
        const auto& persisted_value = *persisted;
        ASSERT_TRUE(std::holds_alternative<std::string>(persisted_value.value));
        EXPECT_EQ(std::get<std::string>(persisted_value.value), *matrix.expected_terminal_value);
      }
    } else {
      EXPECT_FALSE(persisted.has_value());
    }
  }
}

TEST_F(DualClientIntegrationTest, InvalidCommandsSurfaceErrorsWithoutCorruption) {
  const auto client_dir = FindPythonClientDir();
  ASSERT_FALSE(client_dir.empty()) << "Python client bundle not found";
  const auto jubectl = FindJubectlBinary();
  ASSERT_FALSE(jubectl.empty()) << "jubectl binary not found";

  const std::string invalid_key = "invalid-matrix-" + std::to_string(std::random_device{}());

  const auto bad_python = RunPythonClientCommand(
      client_dir, {std::string{"set"}, invalid_key, "bytes", "zz-not-hex"}, port());
  EXPECT_NE(bad_python.exit_code, 0);
  EXPECT_NE(bad_python.output.find("Error"), std::string::npos);

  const auto malformed_payload = SendRawFrameToServer(
      port(), R"({"txn_id":42,"operations":[{"type":"unknown","key":")" + invalid_key + R"("}]})");
  EXPECT_EQ(malformed_payload.exit_code, 0);
  EXPECT_TRUE(malformed_payload.output.empty());

  auto store = jubilant::storage::SimpleStore::Open(temp_dir_);
  EXPECT_EQ(store.size(), 0U);

  const auto recovery =
      RunPythonClient(client_dir, "set", invalid_key, "clean-value", "string", port());
  ASSERT_EQ(recovery.at("state"), "committed");
  const auto confirmation = RunJubectlRemote(jubectl, "get", invalid_key, "", "", port());
  ASSERT_EQ(confirmation.at("state"), "committed");
  ASSERT_FALSE(confirmation.at("operations").empty());
  const auto& confirmation_op = confirmation.at("operations")[0];
  ASSERT_TRUE(confirmation_op.at("success").get<bool>());
  ASSERT_TRUE(confirmation_op.contains("value"));
  EXPECT_EQ(confirmation_op.at("value").at("data").get<std::string>(), "clean-value");

  auto reloaded = jubilant::storage::SimpleStore::Open(temp_dir_);
  const auto final_record = reloaded.Get(invalid_key);
  if (!final_record.has_value()) {
    FAIL() << "Expected " << invalid_key << " to persist clean-value";
    return;
  }
  const auto& final_record_value = *final_record;
  ASSERT_TRUE(std::holds_alternative<std::string>(final_record_value.value));
  EXPECT_EQ(std::get<std::string>(final_record_value.value), "clean-value");
}

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
