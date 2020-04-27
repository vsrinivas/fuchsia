// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/const_value.h"

#include <gtest/gtest.h>

namespace zxdb {

TEST(ConstValue, Empty) {
  ConstValue empty;
  EXPECT_FALSE(empty.has_value());
}

TEST(ConstValue, Numbers) {
  // Numbers are truncated on output.
  ConstValue minus_two(-2);
  EXPECT_TRUE(minus_two.has_value());
  std::vector<uint8_t> minus_two_1{0xfe};
  std::vector<uint8_t> minus_two_2{0xfe, 0xff};
  std::vector<uint8_t> minus_two_4{0xfe, 0xff, 0xff, 0xff};
  std::vector<uint8_t> minus_two_8{0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  EXPECT_EQ(minus_two_1, minus_two.GetConstValue(1));
  EXPECT_EQ(minus_two_2, minus_two.GetConstValue(2));
  EXPECT_EQ(minus_two_4, minus_two.GetConstValue(4));
  EXPECT_EQ(minus_two_8, minus_two.GetConstValue(8));

  // After 64 bits numbers a zero-filled. This isn't necessarily desirable but we assume there
  // aren't constant integers greater than this, and if there are they'll be expressed as a data
  // block (DWARF can't encode these as DW_FORM_*data).
  std::vector<uint8_t> minus_two_10{0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0, 0};
  EXPECT_EQ(minus_two_10, minus_two.GetConstValue(10));
}

TEST(ConstValue, Data) {
  // Random data block.
  ConstValue some_data(std::vector<uint8_t>{1, 2, 3});
  std::vector<uint8_t> some_data_1{1};
  std::vector<uint8_t> some_data_3{1, 2, 3};
  std::vector<uint8_t> some_data_4{1, 2, 3, 0};
  EXPECT_EQ(some_data_1, some_data.GetConstValue(1));
  EXPECT_EQ(some_data_3, some_data.GetConstValue(3));
  EXPECT_EQ(some_data_4, some_data.GetConstValue(4));
}

}  // namespace zxdb
