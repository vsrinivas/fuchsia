// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/modular_config/modular_config_accessor.h"

#include <gtest/gtest.h>

#include "src/modular/lib/modular_config/modular_config.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"

namespace modular {

// Tests that |use_random_session_id| is false if the |auto_login_to_guest| build flag is set.
TEST(ModularConfigAccessor, UseRandomSessionIdBuildFlag) {
  auto accessor = ModularConfigAccessor(fuchsia::modular::session::ModularConfig{});

  // This flag overrides the value for |use_random_session_id|.
  // If it's isn't set, it's not necessarily true.
  if (kUseStableSessionId) {
    EXPECT_FALSE(accessor.use_random_session_id());
  }
}

// Tests that |use_random_session_id| is false if the base shell is configured to persist the user.
TEST(ModularConfigAccessor, UseRandomSessionIdPersistUserArg) {
  // Create a config that has a base shell config with the `--persist-user` arg.
  fuchsia::modular::session::ModularConfig config;
  config.mutable_basemgr_config()->mutable_base_shell()->mutable_app_config()->set_args(
      {modular_config::kPersistUserArg});

  auto accessor = ModularConfigAccessor(std::move(config));

  // Persisting the user means that the session ID should not be random.
  EXPECT_FALSE(accessor.use_random_session_id());
}

// Tests that |use_random_session_id| is true with the default Modular configuration.
TEST(ModularConfigAccessor, UseRandomSessionIdDefault) {
  auto accessor = ModularConfigAccessor(modular::DefaultConfig());
  EXPECT_TRUE(accessor.use_random_session_id());
}

// Tests that |session_shell_app_config| returns the first configured session shell.
TEST(ModularConfigAccessor, SessionShellAppConfigUsesFirstShell) {
  static constexpr auto kFirstSessionShellUrl =
      "fuchsia-pkg://fuchsia.com/first_session_shell#meta/first_session_shell.cmx";
  static constexpr auto kSecondSessionShellUrl =
      "fuchsia-pkg://fuchsia.com/second_session_shell_2#meta/second_session_shell.cmx";

  // Create a config with two session shells.
  fuchsia::modular::session::ModularConfig config;
  auto session_shells = config.mutable_basemgr_config()->mutable_session_shell_map();

  auto first_session_shell = fuchsia::modular::session::SessionShellMapEntry();
  first_session_shell.mutable_config()->mutable_app_config()->set_url(kFirstSessionShellUrl);
  session_shells->push_back(std::move(first_session_shell));

  auto second_session_shell = fuchsia::modular::session::SessionShellMapEntry();
  second_session_shell.mutable_config()->mutable_app_config()->set_url(kSecondSessionShellUrl);
  session_shells->push_back(std::move(second_session_shell));

  auto accessor = ModularConfigAccessor(std::move(config));

  // The config object should be unchanged, containing both session shells.
  ASSERT_EQ(2u, accessor.config().basemgr_config().session_shell_map().size());

  // |session_shell_app_config| should return the first one.
  EXPECT_EQ(kFirstSessionShellUrl, accessor.session_shell_app_config().url());
}

}  // namespace modular
