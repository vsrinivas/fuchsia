// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/core_dev_tools/user_agent.h"

#include <gtest/gtest.h>

namespace analytics::core_dev_tools::internal {

namespace {

constexpr char kToolName[] = "zxdb";
#if defined(__linux__)
constexpr char kExpectedResult[] = "Fuchsia zxdb(Linux)";
#elif defined(__APPLE__)
constexpr char kExpectedResult[] = "Fuchsia zxdb(Macintosh)";
#else
constexpr char kExpectedResult[] = "Fuchsia zxdb";
#endif

}  // namespace

TEST(UserAgentTest, All) { EXPECT_EQ(GenerateUserAgent(kToolName), kExpectedResult); }

}  // namespace analytics::core_dev_tools::internal
