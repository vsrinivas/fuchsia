// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/ui/lib/yuv/yuv.h"

namespace yuv {

TEST(YuvTest, Black) {
  int32_t y = 16;
  int32_t u = 128;
  int32_t v = 128;

  uint8_t bgra[4];
  YuvToBgra(y, u, v, bgra);

  EXPECT_EQ(bgra[0], 0x00);
  EXPECT_EQ(bgra[1], 0x00);
  EXPECT_EQ(bgra[2], 0x00);
  EXPECT_EQ(bgra[3], 0xFF);
}

// Verify if the color is in sRGB space.
TEST(YuvTest, Y16U0V128) {
  int32_t y = 16;
  int32_t u = 0;
  int32_t v = 128;

  uint8_t bgra[4];
  YuvToBgra(y, u, v, bgra);

  EXPECT_EQ(bgra[0], 0x00);
  EXPECT_EQ(bgra[1], 0x5C);
  EXPECT_EQ(bgra[2], 0x00);
  EXPECT_EQ(bgra[3], 0xFF);
}

}  // namespace yuv
