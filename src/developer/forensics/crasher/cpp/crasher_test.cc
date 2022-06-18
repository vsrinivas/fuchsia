// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

namespace {

TEST(CrasherTest, ShouldPass) { ASSERT_TRUE(true); }

TEST(CrasherTest, ShouldFail) { ASSERT_TRUE(false); }

TEST(CrasherTest, WaitAndFail) {
  sleep(10);
  ASSERT_TRUE(false);
}

}  // namespace
