// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>

#include <gtest/gtest.h>

TEST(TestEnviron, TestEnviron) {
  EXPECT_STREQ(std::getenv("HELLO"), "WORLD");
  EXPECT_STREQ(std::getenv("FOO"), "BAR");
}
