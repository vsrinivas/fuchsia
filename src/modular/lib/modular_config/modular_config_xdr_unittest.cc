// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/modular_config/modular_config_xdr.h"

#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/modular/session/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>

#include <algorithm>
#include <cctype>

#include "gtest/gtest.h"
#include "src/lib/files/file.h"

namespace modular {

// Tests that default values are set correctly for BasemgrConfig when reading
// an empty JSON and that JSON values are set correctly when BasemgrConfig
// contains no values.
TEST(ModularConfigXdr, BasemgrDefaultValues) {
  std::string write_json;
  fuchsia::modular::session::BasemgrConfig write_config;
  XdrWrite(&write_json, &write_config, XdrBasemgrConfig);

  std::string expected_json = R"({
    "enable_cobalt":true,
    "use_minfs":true,
    "use_session_shell_for_story_shell_factory":false,
    "base_shell":{
      "url":"fuchsia-pkg://fuchsia.com/auto_login_base_shell#meta/auto_login_base_shell.cmx",
      "keep_alive_after_login":false,
      "args":[]
    },
    "session_shells":[
      {
        "name":"fuchsia-pkg://fuchsia.com/ermine_session_shell#meta/ermine_session_shell.cmx",
        "display_usage":"unknown",
        "screen_height":0.0,
        "screen_width":0.0,
        "url":"fuchsia-pkg://fuchsia.com/ermine_session_shell#meta/ermine_session_shell.cmx",
        "args":[]
      }
    ],
    "story_shell_url":"fuchsia-pkg://fuchsia.com/mondrian#meta/mondrian.cmx"})";

  // Remove whitespace for string comparison
  expected_json.erase(std::remove_if(expected_json.begin(), expected_json.end(), ::isspace),
                      expected_json.end());
  EXPECT_EQ(expected_json, write_json);

  std::string read_json = "\"\"";
  fuchsia::modular::session::BasemgrConfig read_config;
  EXPECT_TRUE(XdrRead(read_json, &read_config, XdrBasemgrConfig));

  EXPECT_TRUE(read_config.enable_cobalt());
  EXPECT_TRUE(read_config.use_minfs());
  EXPECT_FALSE(read_config.use_session_shell_for_story_shell_factory());

  EXPECT_EQ(
      "fuchsia-pkg://fuchsia.com/auto_login_base_shell#meta/"
      "auto_login_base_shell.cmx",
      read_config.base_shell().app_config().url());
  EXPECT_FALSE(read_config.base_shell().keep_alive_after_login());
  EXPECT_EQ(0u, read_config.base_shell().app_config().args().size());

  ASSERT_EQ(1u, read_config.session_shell_map().size());
  EXPECT_EQ(
      "fuchsia-pkg://fuchsia.com/ermine_session_shell#meta/"
      "ermine_session_shell.cmx",
      read_config.session_shell_map().at(0).name());
  EXPECT_EQ(
      "fuchsia-pkg://fuchsia.com/ermine_session_shell#meta/"
      "ermine_session_shell.cmx",
      read_config.session_shell_map().at(0).config().app_config().url());
  EXPECT_EQ(fuchsia::ui::policy::DisplayUsage::kUnknown,
            read_config.session_shell_map().at(0).config().display_usage());
  EXPECT_EQ(0, read_config.session_shell_map().at(0).config().screen_height());
  EXPECT_EQ(0, read_config.session_shell_map().at(0).config().screen_width());
  EXPECT_EQ("fuchsia-pkg://fuchsia.com/mondrian#meta/mondrian.cmx",
            read_config.story_shell().app_config().url());
}

// Tests that default values are set correctly for SessionmgrConfig when reading
// an empty JSON and that JSON values are set correctly when SessionmgrConfig
// contains no values.
TEST(ModularConfigXdr, SessionmgrDefaultValues) {
  std::string write_json;
  fuchsia::modular::session::SessionmgrConfig write_config;
  XdrWrite(&write_json, &write_config, XdrSessionmgrConfig);

  std::string expected_json = R"({
      "cloud_provider":"LET_LEDGER_DECIDE",
      "enable_cobalt":true,
      "enable_story_shell_preload":true,
      "use_memfs_for_ledger":false,
      "startup_agents":null,
      "session_agents":null,
      "component_args":null,
      "agent_service_index":null})";

  // Remove whitespace for string comparison
  expected_json.erase(std::remove_if(expected_json.begin(), expected_json.end(), ::isspace),
                      expected_json.end());
  EXPECT_EQ(expected_json, write_json);

  std::string read_json = "\"\"";
  fuchsia::modular::session::SessionmgrConfig read_config;
  EXPECT_TRUE(XdrRead(read_json, &read_config, XdrSessionmgrConfig));

  EXPECT_EQ(fuchsia::modular::session::CloudProvider::LET_LEDGER_DECIDE,
            read_config.cloud_provider());
  EXPECT_TRUE(read_config.enable_cobalt());
  EXPECT_TRUE(read_config.enable_story_shell_preload());
  EXPECT_FALSE(read_config.use_memfs_for_ledger());

  EXPECT_EQ(0u, read_config.startup_agents().size());
  EXPECT_EQ(0u, read_config.session_agents().size());
}

// Tests that the cloud provider field is read and written correctly when it's not set to the
// default field.
TEST(ModularConfigXdr, CloudProvider) {
  std::string config_str = R"({"cloud_provider":"NONE"})";

  fuchsia::modular::session::SessionmgrConfig read_config;
  EXPECT_TRUE(XdrRead(config_str, &read_config, XdrSessionmgrConfig));
  EXPECT_EQ(fuchsia::modular::session::CloudProvider::NONE, read_config.cloud_provider());

  std::string write_json;
  XdrWrite(&write_json, &read_config, XdrSessionmgrConfig);

  fuchsia::modular::session::SessionmgrConfig read_config_again;
  EXPECT_TRUE(XdrRead(write_json, &read_config_again, XdrSessionmgrConfig));
  EXPECT_EQ(fuchsia::modular::session::CloudProvider::NONE, read_config_again.cloud_provider());
}

}  // namespace modular
