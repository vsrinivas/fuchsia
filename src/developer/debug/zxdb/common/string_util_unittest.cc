// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/string_util.h"

#include "gtest/gtest.h"

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

  // Just one bit of the high 64-bits is set.
  uint128_t sixty_fourth_bit = static_cast<uint128_t>(1) << 64;
  EXPECT_EQ("0x10000000000000000", to_hex_string(sixty_fourth_bit));
  EXPECT_EQ("00010000000000000000", to_hex_string(sixty_fourth_bit, 20, false));

  int128_t minus_one_128 = -1;
  EXPECT_EQ("0xffffffffffffffffffffffffffffffff", to_hex_string(minus_one_128));
}

}  // namespace zxdb
