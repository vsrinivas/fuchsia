// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/expr_value_source.h"

#include <gtest/gtest.h>

namespace zxdb {

TEST(ExprValueSource, SetBits) {
  // Masking with no shift.
  ExprValueSource no_shift_8_bits(0x1000, 8, 0);
  EXPECT_EQ(123u, no_shift_8_bits.SetBits(0, 123));                 // Write random number
  EXPECT_EQ(255u, no_shift_8_bits.SetBits(0, 0xfffffffffu));        // Set all bits
  EXPECT_EQ(0xffffff00u, no_shift_8_bits.SetBits(0xffffffffu, 0));  // Clear all bits.

  // Masking with shift.
  ExprValueSource shift_3_8_bits(0x1000, 8, 3);
  EXPECT_EQ(123u << 3, shift_3_8_bits.SetBits(0, 123));    // Write random number.
  EXPECT_EQ(0xffu << 3, shift_3_8_bits.SetBits(0, 0xff));  // Set all bits.
  EXPECT_EQ(0xf807u, shift_3_8_bits.SetBits(0xffff, 0));
}

}  // namespace zxdb
