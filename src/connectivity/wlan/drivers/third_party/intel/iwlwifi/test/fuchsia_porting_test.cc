// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.
//
// Unittest code for the functions in fuchsia_porting.h.

#include <lib/mock-function/mock-function.h>
#include <stdio.h>

#include <zxtest/zxtest.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fuchsia_porting.h"
}

namespace {

class FuchsiaPortingTest : public zxtest::Test {
 public:
  FuchsiaPortingTest() {}
  ~FuchsiaPortingTest() {}
};

TEST_F(FuchsiaPortingTest, __set_bit) {
  unsigned long masks[] = {0, 0};

  // Test LSB at first addr.
  __set_bit(0, masks);
  EXPECT_EQ(masks[0], 0x0000000000000001);
  EXPECT_EQ(masks[1], 0x0000000000000000);

  // Test MSB at first addr.
  __set_bit(63, masks);
  EXPECT_EQ(masks[0], 0x8000000000000001);
  EXPECT_EQ(masks[1], 0x0000000000000000);

  // Test LSB at 2nd addr.
  __set_bit(64, masks);
  EXPECT_EQ(masks[0], 0x8000000000000001);
  EXPECT_EQ(masks[1], 0x0000000000000001);
}

TEST_F(FuchsiaPortingTest, set_bit) {
  unsigned long masks[] = {0, 0};

  // Test LSB at first addr.
  set_bit(0, masks);
  EXPECT_EQ(masks[0], 0x0000000000000001);
  EXPECT_EQ(masks[1], 0x0000000000000000);

  // Test MSB at first addr.
  set_bit(63, masks);
  EXPECT_EQ(masks[0], 0x8000000000000001);
  EXPECT_EQ(masks[1], 0x0000000000000000);

  // Test LSB at 2nd addr.
  set_bit(64, masks);
  EXPECT_EQ(masks[0], 0x8000000000000001);
  EXPECT_EQ(masks[1], 0x0000000000000001);
}

TEST_F(FuchsiaPortingTest, clear_bit) {
  unsigned long masks[] = {0xffffffffffffffff, 0xffffffffffffffff};

  // Test LSB at first addr.
  clear_bit(0, masks);
  EXPECT_EQ(masks[0], 0xfffffffffffffffe);
  EXPECT_EQ(masks[1], 0xffffffffffffffff);

  // Test MSB at first addr.
  clear_bit(63, masks);
  EXPECT_EQ(masks[0], 0x7ffffffffffffffe);
  EXPECT_EQ(masks[1], 0xffffffffffffffff);

  // Test LSB at 2nd addr.
  clear_bit(64, masks);
  EXPECT_EQ(masks[0], 0x7ffffffffffffffe);
  EXPECT_EQ(masks[1], 0xfffffffffffffffe);
}

}  // namespace
