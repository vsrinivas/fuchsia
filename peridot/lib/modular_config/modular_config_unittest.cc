// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/modular_config/modular_config.h"

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

#include <thread>

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
      files::JoinPath(modular_config::kOverriddenConfigDir,
                      modular_config::kStartupConfigFilePath),
      config_contents));

  modular::ModularConfigReader reader(server.OpenAt("."));
  auto config = reader.GetBasemgrConfig();

  // Verify that ModularConfigReader parsed the config value we gave it.
  EXPECT_EQ(kSessionShellForTest,
            config.session_shell_map().at(0).config().app_config().url());
}

// Test that ModularConfigReader finds and reads the AgentServiceIndex.
TEST_F(ModularConfigReaderTest, ProvideAgentServiceIndex) {
  const std::string kServiceNameForTest =
      "fuchsia.modular.ModularConfigReaderTest";
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
      files::JoinPath(modular_config::kOverriddenConfigDir,
                      modular_config::kStartupConfigFilePath),
      config_contents));

  modular::ModularConfigReader reader(server.OpenAt("."));
  auto config = reader.GetSessionmgrConfig();

  // Verify that ModularConfigReader parsed the config values we gave it.
  EXPECT_EQ(service_name_0, config.agent_service_index().at(0).service_name());
  EXPECT_EQ(agent_url_0, config.agent_service_index().at(0).agent_url());
  EXPECT_EQ(service_name_1, config.agent_service_index().at(1).service_name());
  EXPECT_EQ(agent_url_1, config.agent_service_index().at(1).agent_url());
}
