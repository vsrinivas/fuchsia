// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/modular_config/modular_config.h"

#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/pseudo_file.h>

#include <thread>

#include <peridot/lib/util/pseudo_dir_server.h>
#include <peridot/lib/util/pseudo_dir_utils.h>
#include <src/lib/files/file.h>
#include <src/lib/files/path.h>
#include <src/lib/files/unique_fd.h>
#include <src/modular/lib/modular_config/modular_config_constants.h>

#include "src/lib/fsl/io/fd.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/substitute.h"

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
  EXPECT_TRUE(basemgr_config.base_shell().keep_alive_after_login());

  sessionmgr_config = second_reader.GetSessionmgrConfig();
  EXPECT_EQ(startup_agent, sessionmgr_config.startup_agents().at(0));
  EXPECT_EQ(agent_service_name, sessionmgr_config.agent_service_index().at(0).service_name());
  EXPECT_EQ(agent_url, sessionmgr_config.agent_service_index().at(0).agent_url());
  EXPECT_TRUE(sessionmgr_config.use_memfs_for_ledger());
  EXPECT_EQ(fuchsia::modular::session::CloudProvider::NONE, sessionmgr_config.cloud_provider());
}

TEST_F(ModularConfigReaderTest, GetConfigAsStringDoesntChangeValues) {
  std::string base_shell_url = "fuchsia-pkg://fuchsia.com/dev_base_shell#meta/dev_base_shell.cmx";
  std::string startup_agent = "fuchsia-pkg://fuchsia.com/startup_agent#meta/startup_agent.cmx";
  std::string session_agent = "fuchsia-pkg://fuchsia.com/session_agent#meta/session_agent.cmx";
  std::string agent_service_name = "fuchsia.modular.ModularConfigReaderTest";
  std::string agent_url =
      "fuchsia-pkg://example.com/ModularConfigReaderTest#meta/"
      "ModularConfigReaderTest.cmx";
  std::string session_shell_url = "fuchsia-pkg://fuchsia.com/session_shell#meta/session_shell.cmx";
  std::string story_shell_url = "fuchsia-pkg://fuchsia.com/story_shell#meta/story_shell.cmx";

  fuchsia::modular::session::BasemgrConfig basemgr_config;
  basemgr_config.set_enable_cobalt(false);
  basemgr_config.set_use_minfs(false);
  basemgr_config.set_use_session_shell_for_story_shell_factory(true);
  basemgr_config.mutable_base_shell()->mutable_app_config()->set_url(base_shell_url);
  basemgr_config.mutable_base_shell()->set_keep_alive_after_login(true);
  fuchsia::modular::session::SessionShellConfig session_shell_config;
  session_shell_config.mutable_app_config()->set_url(session_shell_url);
  session_shell_config.set_display_usage(fuchsia::ui::policy::DisplayUsage::kHandheld);
  session_shell_config.set_screen_height(20.f);
  session_shell_config.set_screen_width(30.f);
  fuchsia::modular::session::SessionShellMapEntry session_shell_map_entry;
  session_shell_map_entry.set_name(session_shell_url);
  session_shell_map_entry.set_config(std::move(session_shell_config));
  basemgr_config.mutable_session_shell_map()->push_back(std::move(session_shell_map_entry));
  fuchsia::modular::session::StoryShellConfig story_shell_config;
  story_shell_config.mutable_app_config()->set_url(story_shell_url);
  story_shell_config.mutable_app_config()->mutable_args()->push_back("arg1");
  basemgr_config.set_story_shell(std::move(story_shell_config));

  fuchsia::modular::session::SessionmgrConfig sessionmgr_config;
  sessionmgr_config.set_cloud_provider(fuchsia::modular::session::CloudProvider::NONE);
  sessionmgr_config.set_enable_cobalt(false);
  sessionmgr_config.set_enable_story_shell_preload(false);
  sessionmgr_config.set_use_memfs_for_ledger(true);
  sessionmgr_config.mutable_startup_agents()->push_back(startup_agent);
  sessionmgr_config.mutable_session_agents()->push_back(session_agent);
  sessionmgr_config.set_story_shell_url(story_shell_url);
  fuchsia::modular::session::AppConfig component_arg;
  component_arg.set_url(agent_url);
  component_arg.mutable_args()->push_back("arg2");
  sessionmgr_config.mutable_component_args()->push_back(std::move(component_arg));
  fuchsia::modular::session::AgentServiceIndexEntry agent_entry;
  agent_entry.set_service_name(agent_service_name);
  agent_entry.set_agent_url(agent_url);
  sessionmgr_config.mutable_agent_service_index()->push_back(std::move(agent_entry));

  modular::ModularConfigReader::GetConfigAsString(&basemgr_config, &sessionmgr_config);

  // Check that none of the configs were modified as part of GetConfigAsString
  EXPECT_FALSE(basemgr_config.enable_cobalt());
  EXPECT_FALSE(basemgr_config.use_minfs());
  EXPECT_TRUE(basemgr_config.use_session_shell_for_story_shell_factory());
  EXPECT_EQ(base_shell_url, basemgr_config.base_shell().app_config().url());
  EXPECT_TRUE(basemgr_config.base_shell().keep_alive_after_login());
  ASSERT_EQ(1u, basemgr_config.session_shell_map().size());
  EXPECT_EQ(session_shell_url, basemgr_config.session_shell_map().at(0).name());
  EXPECT_EQ(session_shell_url,
            basemgr_config.session_shell_map().at(0).config().app_config().url());
  EXPECT_TRUE(basemgr_config.session_shell_map().at(0).config().has_display_usage());
  EXPECT_EQ(fuchsia::ui::policy::DisplayUsage::kHandheld,
            basemgr_config.session_shell_map().at(0).config().display_usage());
  EXPECT_EQ(20.f, basemgr_config.session_shell_map().at(0).config().screen_height());
  EXPECT_EQ(30.f, basemgr_config.session_shell_map().at(0).config().screen_width());
  EXPECT_EQ(story_shell_url, basemgr_config.story_shell().app_config().url());
  ASSERT_EQ(1u, basemgr_config.story_shell().app_config().args().size());
  EXPECT_EQ("arg1", basemgr_config.story_shell().app_config().args().at(0));

  EXPECT_EQ(fuchsia::modular::session::CloudProvider::NONE, sessionmgr_config.cloud_provider());
  EXPECT_FALSE(sessionmgr_config.enable_cobalt());
  EXPECT_FALSE(sessionmgr_config.enable_story_shell_preload());
  EXPECT_TRUE(sessionmgr_config.use_memfs_for_ledger());
  EXPECT_EQ(startup_agent, sessionmgr_config.startup_agents().at(0));
  EXPECT_EQ(session_agent, sessionmgr_config.session_agents().at(0));
  EXPECT_EQ(story_shell_url, sessionmgr_config.story_shell_url());
  EXPECT_EQ(agent_url, sessionmgr_config.component_args().at(0).url());
  ASSERT_EQ(1u, sessionmgr_config.component_args().at(0).args().size());
  EXPECT_EQ("arg2", sessionmgr_config.component_args().at(0).args().at(0));
  EXPECT_EQ(agent_service_name, sessionmgr_config.agent_service_index().at(0).service_name());
  EXPECT_EQ(agent_url, sessionmgr_config.agent_service_index().at(0).agent_url());
}
