// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <librtc.h>

#include <zxtest/zxtest.h>

TEST(RTCLibTest, BCD) {
  EXPECT_EQ(0x00, to_bcd(0));
  EXPECT_EQ(0x16, to_bcd(16));
  EXPECT_EQ(0x99, to_bcd(99));

  EXPECT_EQ(0, from_bcd(0x00));
  EXPECT_EQ(16, from_bcd(0x16));
  EXPECT_EQ(99, from_bcd(0x99));
}
