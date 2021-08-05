// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/modular_config/modular_config.h"

#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/pseudo_file.h>

#include <thread>

#include <fbl/unique_fd.h>
#include <rapidjson/document.h>
#include <src/lib/files/directory.h>
#include <src/lib/files/file.h>
#include <src/lib/files/path.h>
#include <src/lib/files/scoped_temp_dir.h>
#include <src/modular/lib/pseudo_dir/pseudo_dir_server.h>
#include <src/modular/lib/pseudo_dir/pseudo_dir_utils.h>

#include "src/lib/fsl/io/fd.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"

// Creates the file at |path| in the directory with file descriptor |root_fd| with
// the contents |data|.
//
// If necessary, creates the |path| directory, including intermediate directories.
bool CreateFileAt(int root_fd, const std::string& path, std::string_view data) {
  if (!files::CreateDirectoryAt(root_fd, files::GetDirectoryName(path))) {
    return false;
  }
  if (!files::WriteFileAt(root_fd, path, data.data(), data.size())) {
    return false;
  }
  return true;
}

class ModularConfigReaderTest : public gtest::RealLoopFixture {};
class ModularConfigWriterTest : public gtest::RealLoopFixture {};

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

// Test that ModularConfigReader uses default values if it fails to read the config data from file.
TEST_F(ModularConfigReaderTest, FailToReadConfigDir) {
  // Create a root directory without a config file.
  modular::PseudoDirServer server(std::make_unique<vfs::PseudoDir>());
  modular::ModularConfigReader reader(server.OpenAt("."));

  auto config = reader.GetBasemgrConfig();
  EXPECT_EQ(modular_config::kDefaultSessionShellUrl,
            config.session_shell_map().at(0).config().app_config().url());
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
  std::string base_shell_url = "fuchsia-pkg://fuchsia.com/test_base_shell#meta/test_base_shell.cmx";
  std::string startup_agent = "fuchsia-pkg://fuchsia.com/startup_agent#meta/startup_agent.cmx";
  std::string agent_service_name = "fuchsia.modular.ModularConfigReaderTest";
  std::string agent_url =
      "fuchsia-pkg://example.com/ModularConfigReaderTest#meta/"
      "ModularConfigReaderTest.cmx";

  std::string config_contents = fxl::Substitute(
      R"({
        "basemgr": {
          "base_shell": {
            "url": "$0",
            "keep_alive_after_login": true
          }
        },
        "sessionmgr": {
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

  // Verify that the second reader has the same config values as the original |config_contents|
  basemgr_config = second_reader.GetBasemgrConfig();
  EXPECT_EQ(base_shell_url, basemgr_config.base_shell().app_config().url());
  EXPECT_TRUE(basemgr_config.base_shell().keep_alive_after_login());

  sessionmgr_config = second_reader.GetSessionmgrConfig();
  EXPECT_EQ(startup_agent, sessionmgr_config.startup_agents().at(0));
  EXPECT_EQ(agent_service_name, sessionmgr_config.agent_service_index().at(0).service_name());
  EXPECT_EQ(agent_url, sessionmgr_config.agent_service_index().at(0).agent_url());
}

TEST_F(ModularConfigReaderTest, GetConfigAsStringDoesntChangeValues) {
  std::string base_shell_url = "fuchsia-pkg://fuchsia.com/test_base_shell#meta/test_base_shell.cmx";
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
  sessionmgr_config.set_enable_cobalt(false);
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
  sessionmgr_config.mutable_restart_session_on_agent_crash()->push_back(agent_url);
  sessionmgr_config.set_disable_agent_restart_on_crash(true);

  modular::ModularConfigReader::GetConfigAsString(&basemgr_config, &sessionmgr_config);

  // Check that none of the configs were modified as part of GetConfigAsString
  EXPECT_FALSE(basemgr_config.enable_cobalt());
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

  EXPECT_FALSE(sessionmgr_config.enable_cobalt());
  EXPECT_EQ(startup_agent, sessionmgr_config.startup_agents().at(0));
  EXPECT_EQ(session_agent, sessionmgr_config.session_agents().at(0));
  EXPECT_EQ(story_shell_url, sessionmgr_config.story_shell_url());
  EXPECT_EQ(agent_url, sessionmgr_config.component_args().at(0).url());
  ASSERT_EQ(1u, sessionmgr_config.component_args().at(0).args().size());
  EXPECT_EQ("arg2", sessionmgr_config.component_args().at(0).args().at(0));
  EXPECT_EQ(agent_service_name, sessionmgr_config.agent_service_index().at(0).service_name());
  EXPECT_EQ(agent_url, sessionmgr_config.agent_service_index().at(0).agent_url());
  EXPECT_EQ(agent_url, sessionmgr_config.restart_session_on_agent_crash().at(0));
  EXPECT_TRUE(sessionmgr_config.disable_agent_restart_on_crash());
}

// Test that ModularConfigReader accepts JSON documents that contain comments
TEST_F(ModularConfigReaderTest, ParseComments) {
  std::string config_contents = R"({
    "basemgr": {
      /* This is
       * a comment */
      "session_shells": [
        {
          // This is another comment
          "url": "fuchsia-pkg://example.com/test#meta/test.cmx"
        }
      ]
    }
  })";

  modular::PseudoDirServer server(modular::MakeFilePathWithContents(
      files::JoinPath(modular_config::kOverriddenConfigDir, modular_config::kStartupConfigFilePath),
      config_contents));

  modular::ModularConfigReader reader(server.OpenAt("."));
  auto config = reader.GetBasemgrConfig();

  // Verify that ModularConfigReader parsed the config.
  EXPECT_EQ(1u, config.session_shell_map().size());
}

// Tests that ParseConfig successfully parses a valid Modular config JSON string with
// some non-default values set.
TEST_F(ModularConfigReaderTest, ParseConfigOk) {
  static constexpr auto kConfigJson = R"({
    "basemgr": {
      "enable_cobalt": false
    },
    "sessionmgr": {
      "enable_cobalt": false
    }
  })";

  auto config_result = modular::ParseConfig(kConfigJson);
  ASSERT_TRUE(config_result.is_ok());

  auto config = config_result.take_value();
  ASSERT_TRUE(config.has_basemgr_config());
  EXPECT_FALSE(config.basemgr_config().enable_cobalt());
  ASSERT_TRUE(config.has_sessionmgr_config());
  EXPECT_FALSE(config.sessionmgr_config().enable_cobalt());
}

// Tests that ParseConfig return an error when passed a string that doesn't contain valid JSON.
TEST_F(ModularConfigReaderTest, ParseConfigInvalidJson) {
  static constexpr auto kConfigJson = R"(this is not valid JSON)";

  auto config_result = modular::ParseConfig(kConfigJson);
  EXPECT_TRUE(config_result.is_error());
}

// Tests that ParseConfig return an error when passed a JSON string that doesn't match
// the Modular config schema.
TEST_F(ModularConfigReaderTest, ParseConfigInvalidSchema) {
  static constexpr auto kConfigJson = R"({
    "basemgr": {
      "session_shells": {
        "this is valid JSON but not a valid modular config"
      }
    }
  })";

  auto config_result = modular::ParseConfig(kConfigJson);
  EXPECT_TRUE(config_result.is_error());
}

// Tests that DefaultConfig returns a ModularConfig with some default values.
TEST_F(ModularConfigReaderTest, DefaultConfig) {
  auto config = modular::DefaultConfig();

  ASSERT_TRUE(config.has_basemgr_config());
  EXPECT_TRUE(config.basemgr_config().enable_cobalt());
  ASSERT_EQ(1u, config.basemgr_config().session_shell_map().size());
  EXPECT_EQ(modular_config::kDefaultSessionShellUrl,
            config.basemgr_config().session_shell_map().at(0).name());

  ASSERT_TRUE(config.has_sessionmgr_config());
  EXPECT_TRUE(config.sessionmgr_config().enable_cobalt());
}

// Tests that ConfigToJsonString returns a JSON string containing a serialized ModularConfig.
TEST_F(ModularConfigReaderTest, ConfigToJsonString) {
  static constexpr auto kExpectedJson = R"({
      "basemgr": {
        "enable_cobalt": true,
        "use_session_shell_for_story_shell_factory": false,
        "base_shell": {
          "url": "fuchsia-pkg://fuchsia.com/auto_login_base_shell#meta/auto_login_base_shell.cmx",
          "keep_alive_after_login": false,
          "args": []
        },
        "session_shells": [
          {
            "name": "fuchsia-pkg://fuchsia.com/dev_session_shell#meta/dev_session_shell.cmx",
            "display_usage": "unknown",
            "screen_height": 0.0,
            "screen_width": 0.0,
            "url": "fuchsia-pkg://fuchsia.com/dev_session_shell#meta/dev_session_shell.cmx",
            "args": []
          }
        ],
        "story_shell_url": "fuchsia-pkg://fuchsia.com/dev_story_shell#meta/dev_story_shell.cmx"
      },
      "sessionmgr": {
        "enable_cobalt": true,
        "startup_agents": [],
        "session_agents": [],
        "component_args": [],
        "agent_service_index": [],
        "restart_session_on_agent_crash": [],
        "disable_agent_restart_on_crash": false
      }
    })";
  rapidjson::Document expected_json_doc;
  expected_json_doc.Parse(kExpectedJson);
  ASSERT_FALSE(expected_json_doc.HasParseError());

  fuchsia::modular::session::ModularConfig config;
  auto config_json = modular::ConfigToJsonString(config);

  rapidjson::Document config_json_doc;
  config_json_doc.Parse(config_json);
  EXPECT_FALSE(config_json_doc.HasParseError());

  EXPECT_EQ(expected_json_doc, config_json_doc)
      << "Expected: " << kExpectedJson << "\nActual: " << config_json;
}

// Tests that ModularConfigReader reads from the persistent configuration when it exists and
// persistent config_override is allowed.
TEST_F(ModularConfigReaderTest, ReadPersistentConfig) {
  constexpr char kSessionShellForTest[] =
      "fuchsia-pkg://example.com/ModularConfigReaderTest#meta/"
      "ModularConfigReaderTest.cmx";

  std::string persistent_config_contents = fxl::Substitute(
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

  // Create a directory that simulates the component namespace with the layout:
  //
  //    /
  //    ├── cache
  //    │   └── startup.config
  //    └── config
  //        └── data
  //            ├── startup.config
  //            └── allow_persistent_config_override
  files::ScopedTempDir root_dir;
  fbl::unique_fd root_fd(open(root_dir.path().c_str(), O_RDONLY));

  // The persistent /cache/startup.config file contains |persistent_config_contents|.
  ASSERT_TRUE(CreateFileAt(root_fd.get(), modular::ModularConfigReader::GetPersistentConfigPath(),
                           persistent_config_contents));

  // The /config/data/startup.config file contains an empty config.
  ASSERT_TRUE(
      CreateFileAt(root_fd.get(), modular::ModularConfigReader::GetDefaultConfigPath(), "{}"));

  // Allow persistent config_override.
  ASSERT_TRUE(CreateFileAt(root_fd.get(),
                           modular::ModularConfigReader::GetAllowPersistentConfigOverridePath(),
                           "(file contents are ignored)"));

  modular::ModularConfigReader reader(std::move(root_fd));

  // Verify that ModularConfigReader read from the persistent config in /cache.
  auto basemgr_config = reader.GetBasemgrConfig();
  EXPECT_EQ(kSessionShellForTest,
            basemgr_config.session_shell_map().at(0).config().app_config().url());
}

// Tests that ModularConfigReader reads from the regular config_override when persistence is
// not allowed even if a persistent config exists.
TEST_F(ModularConfigReaderTest, ReadPersistentConfigNotAllowed) {
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

  // Create a directory that simulates the component namespace with the layout:
  //
  //    /
  //    ├── cache
  //    │   └── startup.config
  //    ├── config
  //    │   └── data
  //    └── config_override
  //        └── data
  //            └── startup.config
  files::ScopedTempDir root_dir;
  fbl::unique_fd root_fd(open(root_dir.path().c_str(), O_RDONLY));

  // The persistent /cache/startup.config file exists but its contents won't be read.
  ASSERT_TRUE(CreateFileAt(root_fd.get(), modular::ModularConfigReader::GetPersistentConfigPath(),
                           "(file contents are ignored)"));

  // The /config_override/data/startup.config file contains |config_contents|.
  ASSERT_TRUE(CreateFileAt(root_fd.get(), modular::ModularConfigReader::GetOverriddenConfigPath(),
                           config_contents));

  // Create the directory where the allow_persistent_config_override marker file
  // should be, but not the file itself.
  ASSERT_TRUE(files::CreateDirectoryAt(
      root_fd.get(), files::GetDirectoryName(
                         modular::ModularConfigReader::GetAllowPersistentConfigOverridePath())));

  modular::ModularConfigReader reader(std::move(root_fd));

  // Verify that ModularConfigReader read from /config_override.
  auto basemgr_config = reader.GetBasemgrConfig();
  EXPECT_EQ(kSessionShellForTest,
            basemgr_config.session_shell_map().at(0).config().app_config().url());
}

// Tests that ModularConfigReader.ReadAndMaybePersistConfig stores configuration from
// config_override to the persistent config dir when allowed.
TEST_F(ModularConfigReaderTest, ReadAndMaybePersistConfig) {
  // Create a directory that simulates the component namespace with the layout:
  //
  //    /
  //    ├── cache
  //    ├── config
  //    │   └── data
  //    │       └── allow_persistent_config_override
  //    └── config_override
  //        └── data
  //            └── startup.config
  files::ScopedTempDir root_dir;
  fbl::unique_fd root_fd(open(root_dir.path().c_str(), O_RDONLY));

  auto persistent_config_dir =
      files::GetDirectoryName(modular::ModularConfigReader::GetPersistentConfigPath());

  // Create the directory where the persistent config should be, but not the file itself.
  ASSERT_TRUE(files::CreateDirectoryAt(root_fd.get(), persistent_config_dir));

  // The /config_override/data/startup.config file contains an empty config.
  ASSERT_TRUE(
      CreateFileAt(root_fd.get(), modular::ModularConfigReader::GetOverriddenConfigPath(), "{}"));

  // Allow persistent config_override.
  ASSERT_TRUE(CreateFileAt(root_fd.get(),
                           modular::ModularConfigReader::GetAllowPersistentConfigOverridePath(),
                           "(file contents are ignored)"));

  modular::ModularConfigReader reader(root_fd.duplicate());

  fbl::unique_fd write_fd(
      openat(root_fd.get(), persistent_config_dir.c_str(), O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(write_fd.is_valid());
  modular::ModularConfigWriter writer(std::move(write_fd));

  auto config_result = reader.ReadAndMaybePersistConfig(&writer);
  ASSERT_TRUE(config_result.is_ok());

  // Verify that ReadAndMaybePersistConfig persisted the configuration.
  EXPECT_TRUE(reader.PersistentConfigExists());
  EXPECT_TRUE(
      files::IsFileAt(root_fd.get(), modular::ModularConfigReader::GetPersistentConfigPath()));
}

// Tests that ModularConfigReader.ReadAndMaybePersistConfig does not store configuration from
// config_override to the persistent config dir when not allowed.
TEST_F(ModularConfigReaderTest, ReadAndMaybePersistConfigNotAllowed) {
  // Create a directory that simulates the component namespace with the layout:
  //
  //    /
  //    ├── cache
  //    ├── config
  //    │   └── data
  //    └── config_override
  //        └── data
  //            └── startup.config
  files::ScopedTempDir root_dir;
  fbl::unique_fd root_fd(open(root_dir.path().c_str(), O_RDONLY));

  auto persistent_config_dir =
      files::GetDirectoryName(modular::ModularConfigReader::GetPersistentConfigPath());

  // Create the directory where the persistent config should be, but not the file itself.
  ASSERT_TRUE(files::CreateDirectoryAt(root_fd.get(), persistent_config_dir));

  // The /config_override/data/startup.config file contains an empty config.
  ASSERT_TRUE(
      CreateFileAt(root_fd.get(), modular::ModularConfigReader::GetOverriddenConfigPath(), "{}"));

  // Create the directory where the allow_persistent_config_override marker file
  // should be, but not the file itself.
  ASSERT_TRUE(files::CreateDirectoryAt(
      root_fd.get(), files::GetDirectoryName(
                         modular::ModularConfigReader::GetAllowPersistentConfigOverridePath())));

  modular::ModularConfigReader reader(root_fd.duplicate());

  fbl::unique_fd write_fd(
      openat(root_fd.get(), persistent_config_dir.c_str(), O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(write_fd.is_valid());
  modular::ModularConfigWriter writer(std::move(write_fd));

  auto config_result = reader.ReadAndMaybePersistConfig(&writer);
  ASSERT_TRUE(config_result.is_ok());

  // Verify that ReadAndMaybePersistConfig has not persisted the configuration.
  EXPECT_FALSE(reader.PersistentConfigExists());
  EXPECT_FALSE(
      files::IsFileAt(root_fd.get(), modular::ModularConfigReader::GetPersistentConfigPath()));
}

// Tests that ModularConfigReader.ReadAndMaybePersistConfig overwrites existing
// persistent configuration.
TEST_F(ModularConfigReaderTest, ReadAndMaybePersistConfigOverwritesExisting) {
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

  // Create a directory that simulates the component namespace with the layout:
  //
  //    /
  //    ├── cache
  //    │   └── startup.config
  //    ├── config
  //    │   └── data
  //    │       └── allow_persistent_config_override
  //    └── config_override
  //        └── data
  //            └── startup.config
  files::ScopedTempDir root_dir;
  fbl::unique_fd root_fd(open(root_dir.path().c_str(), O_RDONLY));

  auto persistent_config_dir =
      files::GetDirectoryName(modular::ModularConfigReader::GetPersistentConfigPath());

  // The persistent /cache/startup.config file contains an empty config.
  ASSERT_TRUE(
      CreateFileAt(root_fd.get(), modular::ModularConfigReader::GetPersistentConfigPath(), "{}"));

  // The /config_override/data/startup.config file contains a config with |kSessionShellForTest|.
  ASSERT_TRUE(CreateFileAt(root_fd.get(), modular::ModularConfigReader::GetOverriddenConfigPath(),
                           config_contents));

  // Allow persistent config_override.
  ASSERT_TRUE(CreateFileAt(root_fd.get(),
                           modular::ModularConfigReader::GetAllowPersistentConfigOverridePath(),
                           "(file contents are ignored)"));

  modular::ModularConfigReader reader(root_fd.duplicate());

  // The persistent config exists before ReadAndMaybePersistConfig is called.
  ASSERT_TRUE(reader.PersistentConfigExists());
  ASSERT_TRUE(
      files::IsFileAt(root_fd.get(), modular::ModularConfigReader::GetPersistentConfigPath()));

  fbl::unique_fd write_fd(
      openat(root_fd.get(), persistent_config_dir.c_str(), O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(write_fd.is_valid());
  modular::ModularConfigWriter writer(std::move(write_fd));

  auto config_result = reader.ReadAndMaybePersistConfig(&writer);
  ASSERT_TRUE(config_result.is_ok());

  // Read the config with a new reader.
  modular::ModularConfigReader reader2(root_fd.duplicate());

  ASSERT_TRUE(reader2.PersistentConfigExists());

  // Verify that ModularConfigReader read from overwritten config.
  auto basemgr_config = reader2.GetBasemgrConfig();
  EXPECT_EQ(kSessionShellForTest,
            basemgr_config.session_shell_map().at(0).config().app_config().url());
}

// Tests that ModularConfigWriter.Delete deletes the persistent configuration file.
TEST_F(ModularConfigWriterTest, Delete) {
  // Create a directory that simulates the component namespace with the layout:
  //
  //    /
  //    └── cache
  //        └── startup.config
  files::ScopedTempDir root_dir;
  fbl::unique_fd root_fd(open(root_dir.path().c_str(), O_RDONLY));

  auto persistent_config_dir =
      files::GetDirectoryName(modular::ModularConfigReader::GetPersistentConfigPath());

  // The persistent /cache/startup.config file contains an empty config.
  ASSERT_TRUE(
      CreateFileAt(root_fd.get(), modular::ModularConfigReader::GetPersistentConfigPath(), "{}"));

  modular::ModularConfigReader reader(root_fd.duplicate());

  fbl::unique_fd write_fd(
      openat(root_fd.get(), persistent_config_dir.c_str(), O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(write_fd.is_valid());
  modular::ModularConfigWriter writer(std::move(write_fd));

  // The persistent config exists before Delete is called.
  ASSERT_TRUE(reader.PersistentConfigExists());
  ASSERT_TRUE(
      files::IsFileAt(root_fd.get(), modular::ModularConfigReader::GetPersistentConfigPath()));

  writer.Delete();

  // The config should have been deleted.
  EXPECT_FALSE(reader.PersistentConfigExists());
  EXPECT_FALSE(
      files::IsFileAt(root_fd.get(), modular::ModularConfigReader::GetPersistentConfigPath()));
}
