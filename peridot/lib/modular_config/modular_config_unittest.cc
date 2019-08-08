// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/modular_config/modular_config.h"

#include <thread>

#include <lib/fsl/io/fd.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/pseudo_file.h>
#include <peridot/lib/modular_config/modular_config_constants.h>
#include <peridot/lib/util/pseudo_dir_server.h>
#include <peridot/lib/util/pseudo_dir_utils.h>
#include <src/lib/files/file.h>
#include <src/lib/files/path.h>
#include <src/lib/files/unique_fd.h>
#include <src/lib/fxl/strings/split_string.h>
#include <src/lib/fxl/strings/substitute.h>

class ModularConfigReaderTest : public gtest::RealLoopFixture {};

// Test that ModularConfigReader finds and reads the startup.config file given a
// root directory that contains config data.
TEST_F(ModularConfigReaderTest, OverrideConfigDir) {
  constexpr char kSessionShellForTest[] =
      "fuchsia-pkg://example.com/ModularConfigReaderTest#meta/"
      "ModularConfigReaderTest.cmx";

  std::string config_contents = fxl::Substitute(
      R"({
        "basemgr": {
          "session_shells": [
            {
              "url": "$0"
            }
          ]
        }
      })",
      kSessionShellForTest);

  modular::PseudoDirServer server(modular::MakeFilePathWithContents(
      files::JoinPath(modular_config::kOverriddenConfigDir, modular_config::kStartupConfigFilePath),
      config_contents));

  modular::ModularConfigReader reader(server.OpenAt("."));
  auto config = reader.GetBasemgrConfig();

  // Verify that ModularConfigReader parsed the config value we gave it.
  EXPECT_EQ(kSessionShellForTest, config.session_shell_map().at(0).config().app_config().url());
}

// Test that ModularConfigReader finds and reads the AgentServiceIndex.
TEST_F(ModularConfigReaderTest, ProvideAgentServiceIndex) {
  const std::string kServiceNameForTest = "fuchsia.modular.ModularConfigReaderTest";
  const std::string kAgentUrlForTest =
      "fuchsia-pkg://example.com/ModularConfigReaderTest#meta/"
      "ModularConfigReaderTest.cmx";

  const std::string service_name_0 = kServiceNameForTest + "0";
  const std::string agent_url_0 = kAgentUrlForTest + "0";
  const std::string service_name_1 = kServiceNameForTest + "1";
  const std::string agent_url_1 = kAgentUrlForTest + "1";

  std::string config_contents = fxl::Substitute(
      R"({
        "basemgr": {
        },
        "sessionmgr": {
          "agent_service_index": [
            {
              "service_name": "$0",
              "agent_url": "$1"
            },
            {
              "service_name": "$2",
              "agent_url": "$3"
            }
          ]
        }
      })",
      service_name_0, agent_url_0, service_name_1, agent_url_1);

  modular::PseudoDirServer server(modular::MakeFilePathWithContents(
      files::JoinPath(modular_config::kOverriddenConfigDir, modular_config::kStartupConfigFilePath),
      config_contents));

  modular::ModularConfigReader reader(server.OpenAt("."));
  auto config = reader.GetSessionmgrConfig();

  // Verify that ModularConfigReader parsed the config values we gave it.
  EXPECT_EQ(service_name_0, config.agent_service_index().at(0).service_name());
  EXPECT_EQ(agent_url_0, config.agent_service_index().at(0).agent_url());
  EXPECT_EQ(service_name_1, config.agent_service_index().at(1).service_name());
  EXPECT_EQ(agent_url_1, config.agent_service_index().at(1).agent_url());
}

TEST_F(ModularConfigReaderTest, GetConfigAsString) {
  std::string base_shell_url = "fuchsia-pkg://fuchsia.com/dev_base_shell#meta/dev_base_shell.cmx";
  std::string startup_agent = "fuchsia-pkg://fuchsia.com/startup_agent#meta/startup_agent.cmx";
  std::string agent_service_name = "fuchsia.modular.ModularConfigReaderTest";
  std::string agent_url =
      "fuchsia-pkg://example.com/ModularConfigReaderTest#meta/"
      "ModularConfigReaderTest.cmx";

  std::string config_contents = fxl::Substitute(
      R"({
        "basemgr": {
          "test": true,
          "base_shell": {
            "url": "$0",
            "keep_alive_after_login": true
          }
        },
        "sessionmgr": {
          "cloud_provider": "NONE",
          "use_memfs_for_ledger": true,
          "startup_agents": [
            "$1"
          ],
          "agent_service_index": [
            {
              "service_name": "$2",
              "agent_url": "$3"
            }
          ]
        }
      })",
      base_shell_url, startup_agent, agent_service_name, agent_url);

  // Host |config_contents|, parse it into |first_reader|, and write configs into |read_config_str|
  modular::PseudoDirServer server(modular::MakeFilePathWithContents(
      files::JoinPath(modular_config::kDefaultConfigDir, modular_config::kStartupConfigFilePath),
      config_contents));

  modular::ModularConfigReader first_reader(server.OpenAt("."));
  auto basemgr_config = first_reader.GetBasemgrConfig();
  auto sessionmgr_config = first_reader.GetSessionmgrConfig();
  auto read_config_str =
      modular::ModularConfigReader::GetConfigAsString(&basemgr_config, &sessionmgr_config);

  // Host the new config string and parse it into |second_reader|
  modular::PseudoDirServer server_after_read(modular::MakeFilePathWithContents(
      files::JoinPath(modular_config::kDefaultConfigDir, modular_config::kStartupConfigFilePath),
      read_config_str));

  modular::ModularConfigReader second_reader(server_after_read.OpenAt("."));

  // Verify that the second reader has the same same config values as the original |config_contents|
  basemgr_config = second_reader.GetBasemgrConfig();
  EXPECT_EQ(base_shell_url, basemgr_config.base_shell().app_config().url());
  EXPECT_TRUE(basemgr_config.test());
  EXPECT_TRUE(basemgr_config.base_shell().keep_alive_after_login());

  sessionmgr_config = second_reader.GetSessionmgrConfig();
  EXPECT_EQ(startup_agent, sessionmgr_config.startup_agents().at(0));
  EXPECT_EQ(agent_service_name, sessionmgr_config.agent_service_index().at(0).service_name());
  EXPECT_EQ(agent_url, sessionmgr_config.agent_service_index().at(0).agent_url());
  EXPECT_TRUE(sessionmgr_config.use_memfs_for_ledger());
  EXPECT_EQ(fuchsia::modular::session::CloudProvider::NONE, sessionmgr_config.cloud_provider());
}
