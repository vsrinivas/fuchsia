// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

// Only one of the two tests will fail.
TEST(FakeFailingTest, Pass) {}

TEST(FakeFailingTest, Fail) {
  EXPECT_EQ(0, 1);
  EXPECT_EQ(0, -1);
}
