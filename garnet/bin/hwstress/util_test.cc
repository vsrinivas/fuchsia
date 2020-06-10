// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <lib/zx/time.h>

#include <cmath>

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

TEST(Util, DurationToSecs) {
  EXPECT_EQ(DurationToSecs(zx::sec(0)), 0.0);
  EXPECT_EQ(DurationToSecs(zx::sec(1)), 1.0);
  EXPECT_EQ(DurationToSecs(zx::msec(1500)), 1.5);
  EXPECT_EQ(DurationToSecs(zx::msec(100)), 0.1);
  EXPECT_EQ(DurationToSecs(zx::sec(-1)), -1.0);
}

TEST(Util, DoubleAsHex) {
  // Simple values.
  EXPECT_EQ(DoubleAsHex(1.0), "0x3ff0000000000000");
  EXPECT_EQ(DoubleAsHex(3.0), "0x4008000000000000");
  EXPECT_EQ(DoubleAsHex(3.14159265358979323846), "0x400921fb54442d18");

  // Positive and negative 0 have different representations.
  EXPECT_EQ(DoubleAsHex(0.0), "0x0000000000000000");
  EXPECT_EQ(DoubleAsHex(-0.0), "0x8000000000000000");

  // NaN.
  EXPECT_EQ(DoubleAsHex(nan("")), "0x7ff8000000000000");
}

}  // namespace
}  // namespace hwstress
