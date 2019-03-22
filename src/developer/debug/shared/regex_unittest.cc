// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/regex.h"

#include <gtest/gtest.h>

namespace debug_ipc {

TEST(Regex, CaseInsensitive) {
  Regex regex;
  // Inser
  ASSERT_TRUE(regex.Init("test"));

  // Init again should fail.
  EXPECT_FALSE(regex.Init("test"));

  EXPECT_TRUE(regex.Match("test"));
  EXPECT_FALSE(regex.Match("bla"));
  EXPECT_TRUE(regex.Match("aaaaTESTaaaa"));
}

TEST(Regex, CaseSensitive) {
  Regex regex;
  ASSERT_TRUE(regex.Init("TEST.*test", Regex::CompareType::kCaseSensitive));

  // Init again should fail.
  EXPECT_FALSE(regex.Init("test"));

  EXPECT_FALSE(regex.Match("test"));
  EXPECT_FALSE(regex.Match("TEST"));
  EXPECT_FALSE(regex.Match("TESTaaaTEST"));
  EXPECT_TRUE(regex.Match("TESTaaatest"));
}

}  // namespace debug_ipc
