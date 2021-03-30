// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/string_util.h"

#include <gtest/gtest.h>

namespace zxdb {

TEST(StringUtil, to_hex_string) {
  EXPECT_EQ("0xf", to_hex_string(static_cast<int8_t>(0xf)));
  EXPECT_EQ("0f", to_hex_string(static_cast<uint8_t>(0xf), 2, false));
  EXPECT_EQ("0xffff", to_hex_string(static_cast<int16_t>(-1)));
  EXPECT_EQ("0x00ffff", to_hex_string(static_cast<uint16_t>(-1), 6, true));
  EXPECT_EQ("0xfffffffe", to_hex_string(static_cast<int32_t>(0xfffffffe)));
  EXPECT_EQ("f", to_hex_string(static_cast<uint32_t>(0xf), 0, false));
  EXPECT_EQ("0xf", to_hex_string(static_cast<int64_t>(0xf)));
  EXPECT_EQ("ffff", to_hex_string(static_cast<uint64_t>(0xffff), 2, false));
  EXPECT_EQ("0x1", to_hex_string(static_cast<uint128_t>(1)));
  EXPECT_EQ("0xffffffffffffffffffffffffffffffff", to_hex_string(static_cast<int128_t>(-1)));

  // Just one bit of the high 64-bits is set.
  uint128_t sixty_fourth_bit = static_cast<uint128_t>(1) << 64;
  EXPECT_EQ("0x10000000000000000", to_hex_string(sixty_fourth_bit));
  EXPECT_EQ("00010000000000000000", to_hex_string(sixty_fourth_bit, 20, false));

  int128_t minus_one_128 = -1;
  EXPECT_EQ("0xffffffffffffffffffffffffffffffff", to_hex_string(minus_one_128));
}

TEST(StringUtil, to_bin_string) {
  EXPECT_EQ("0", to_bin_string(0, 0, false));
  EXPECT_EQ("0b0", to_bin_string(0, 0, true));
  EXPECT_EQ("000", to_bin_string(0, 3, false));
  EXPECT_EQ("0b000", to_bin_string(0, 3, true));
  EXPECT_EQ("0b10000000", to_bin_string(0b10000000));
  EXPECT_EQ("0b11110000111000001100000010000000",
            to_bin_string(0b11110000111000001100000010000000u));

  // Unneeded byte separator
  EXPECT_EQ("0b10000000", to_bin_string(0b10000000, 0, true, '.'));

  // Padding beyond type size.
  EXPECT_EQ("0b000010000000", to_bin_string(static_cast<uint8_t>(0b10000000), 12));
  EXPECT_EQ("0b0000'10000000", to_bin_string(static_cast<uint8_t>(0b10000000), 12, true, '\''));

  EXPECT_EQ("0b1111111111111111", to_bin_string(static_cast<int16_t>(-1)));
  EXPECT_EQ("11111111,11111111,11111111,11111111",
            to_bin_string(static_cast<int32_t>(-1), 0, false, ','));

  uint128_t high_bit_128 = static_cast<uint128_t>(1) << 127;
  EXPECT_EQ(
      "0b1000000000000000000000000000000000000000000000000000000000000000"
      "0000000000000000000000000000000000000000000000000000000000000000",
      to_bin_string(high_bit_128));

  int128_t minus_one_128 = -1;
  EXPECT_EQ(
      "0b1111111111111111111111111111111111111111111111111111111111111111"
      "1111111111111111111111111111111111111111111111111111111111111111",
      to_bin_string(minus_one_128));
}

}  // namespace zxdb
