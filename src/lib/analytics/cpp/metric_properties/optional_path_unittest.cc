// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/metric_properties/optional_path.h"

#include <cstdlib>

#include <gtest/gtest.h>

namespace analytics::metric_properties::internal {

TEST(OptionalPathTest, NoEnv) {
  ASSERT_EQ(unsetenv("FUCHSIA_ANALYTICS_TEST_ENV"), 0);
  auto path = GetOptionalPathFromEnv("FUCHSIA_ANALYTICS_TEST_ENV");
  EXPECT_FALSE(path.has_value());
}

TEST(OptionalPathTest, HasEnv) {
  ASSERT_EQ(setenv("FUCHSIA_ANALYTICS_TEST_ENV", "foo/bar", 1), 0);
  auto path = GetOptionalPathFromEnv("FUCHSIA_ANALYTICS_TEST_ENV");
  EXPECT_TRUE(path.has_value());
  EXPECT_STREQ(path->c_str(), "foo/bar");
}

}  // namespace analytics::metric_properties::internal
