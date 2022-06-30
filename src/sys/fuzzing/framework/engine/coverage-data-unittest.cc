// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/coverage-data.h"

#include <gtest/gtest.h>

namespace fuzzing {

TEST(CoverageDataTest, GetTargetId) {
  EXPECT_EQ(GetTargetId("invalid="), kInvalidTargetId);
  // Compare with `echo 'a+target+id=' | base64 -d | xxd -e -g8`.
  EXPECT_EQ(GetTargetId("a+target+id"), 0x27faad07ae5aeb6bULL);
  EXPECT_EQ(GetTargetId("a+target+id="), 0x27faad07ae5aeb6bULL);
  EXPECT_EQ(GetTargetId("a+target+id+and+a+module+id="), 0x27faad07ae5aeb6bULL);
}

TEST(CoverageDataTest, GetModuleId) {
  EXPECT_EQ(GetModuleId("invalid="), "");
  EXPECT_EQ(GetModuleId("a+target+id"), "");
  EXPECT_EQ(GetModuleId("a+target+id="), "=");
  EXPECT_EQ(GetModuleId("a+target+id+and+a+module+id="), "+and+a+module+id=");
}

}  // namespace fuzzing
