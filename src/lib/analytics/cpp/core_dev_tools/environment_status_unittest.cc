// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/core_dev_tools/environment_status.h"

#include <cstdlib>

#include <gtest/gtest.h>

namespace analytics::core_dev_tools {

namespace {

TEST(EnvironmentStatusTest, All) {
  ASSERT_EQ(setenv("TEST_ONLY_ENV", "1", 1), 0);
  EXPECT_STREQ(GetBotInfo().name, "test-only");
  EXPECT_TRUE(IsRunByBot());
}

}  // namespace

}  // namespace analytics::core_dev_tools
