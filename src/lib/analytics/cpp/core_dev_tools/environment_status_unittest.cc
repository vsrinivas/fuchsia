// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/core_dev_tools/environment_status.h"

#include <cstdlib>

#include <gtest/gtest.h>

namespace analytics::core_dev_tools {

namespace {

TEST(EnvironmentStatusTest, Bot) {
  ASSERT_EQ(setenv("TEST_ONLY_ENV", "1", 1), 0);
  EXPECT_STREQ(GetBotInfo().name, "test-only");
  EXPECT_TRUE(IsRunByBot());
}

TEST(EnvironmentStatusTest, Disabled) {
  ASSERT_EQ(unsetenv("FUCHSIA_ANALYTICS_DISABLED"), 0);
  EXPECT_FALSE(IsDisabledByEnvironment());

  ASSERT_EQ(setenv("FUCHSIA_ANALYTICS_DISABLED", "1", 1), 0);
  EXPECT_TRUE(IsDisabledByEnvironment());
  // IsDisabledByEnvironment() currently returns true whenever FUCHSIA_ANALYTICS_DISABLED is set,
  // no matter what value it is. This behavior should be consistent with
  // //src/lib/analytics/rust/src/env_info.rs
  ASSERT_EQ(setenv("FUCHSIA_ANALYTICS_DISABLED", "0", 1), 0);
  EXPECT_TRUE(IsDisabledByEnvironment());
  ASSERT_EQ(setenv("FUCHSIA_ANALYTICS_DISABLED", "", 1), 0);
  EXPECT_TRUE(IsDisabledByEnvironment());

  ASSERT_EQ(unsetenv("FUCHSIA_ANALYTICS_DISABLED"), 0);
  EXPECT_FALSE(IsDisabledByEnvironment());
}

}  // namespace

}  // namespace analytics::core_dev_tools
