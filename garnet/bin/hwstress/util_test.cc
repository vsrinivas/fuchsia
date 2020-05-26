// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <lib/zx/time.h>

#include <gtest/gtest.h>

namespace hwstress {
namespace {

TEST(Util, SecsToDuration) {
  EXPECT_EQ(SecsToDuration(0.0), zx::sec(0));
  EXPECT_EQ(SecsToDuration(1.0), zx::sec(1));
  EXPECT_EQ(SecsToDuration(1.5), zx::msec(1500));
  EXPECT_EQ(SecsToDuration(0.1), zx::msec(100));
  EXPECT_EQ(SecsToDuration(-1.0), zx::sec(-1));
}

TEST(Util, DurationToSEcs) {
  EXPECT_EQ(DurationToSecs(zx::sec(0)), 0.0);
  EXPECT_EQ(DurationToSecs(zx::sec(1)), 1.0);
  EXPECT_EQ(DurationToSecs(zx::msec(1500)), 1.5);
  EXPECT_EQ(DurationToSecs(zx::msec(100)), 0.1);
  EXPECT_EQ(DurationToSecs(zx::sec(-1)), -1.0);
}

}  // namespace
}  // namespace hwstress
