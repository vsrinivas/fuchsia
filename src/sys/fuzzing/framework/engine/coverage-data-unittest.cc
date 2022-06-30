// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/coverage-data.h"

#include <gtest/gtest.h>

namespace fuzzing {

TEST(CoverageDataTest, GetTargetId) {
  EXPECT_EQ(GetTargetId("12e"), kInvalidTargetId);
  EXPECT_EQ(GetTargetId("123"), 123U);
  EXPECT_EQ(GetTargetId("123/foo.bar"), 123U);
}

TEST(CoverageDataTest, GetModuleId) {
  EXPECT_EQ(GetModuleId("ignored"), "");
  EXPECT_EQ(GetModuleId("ignored/foo.bar"), "foo.bar");
}

}  // namespace fuzzing
