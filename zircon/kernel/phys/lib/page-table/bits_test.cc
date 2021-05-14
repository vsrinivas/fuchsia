// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/page-table/internal/bits.h"

#include <stdlib.h>

#include <gtest/gtest.h>

namespace page_table::internal {
namespace {

TEST(Bits, MaxAlignmentBits) {
  EXPECT_EQ(MaxAlignmentBits(0), 64u);
  EXPECT_EQ(MaxAlignmentBits(1), 0u);
  EXPECT_EQ(MaxAlignmentBits(2), 1u);
  EXPECT_EQ(MaxAlignmentBits(3), 0u);
  EXPECT_EQ(MaxAlignmentBits(4), 2u);
  EXPECT_EQ(MaxAlignmentBits(0x100), 8u);
  EXPECT_EQ(MaxAlignmentBits(0x8000'0000'0000'0000), 63u);
}

TEST(Bits, IsPow2) {
  EXPECT_TRUE(IsPow2(1));
  EXPECT_TRUE(IsPow2(2));
  EXPECT_TRUE(IsPow2(64));
  EXPECT_TRUE(IsPow2(0x8000'0000'0000'0000));

  EXPECT_FALSE(IsPow2(0));
  EXPECT_FALSE(IsPow2(3));
  EXPECT_FALSE(IsPow2(0xffff'ffff'ffff'ffff));
}

TEST(Bits, IsAligned) {
  // 0 is aligned to everything.
  EXPECT_TRUE(IsAligned(0, 1));
  EXPECT_TRUE(IsAligned(0, 2));
  EXPECT_TRUE(IsAligned(0, 0x8000'0000'0000'0000));

  // Everything is aligned to 1.
  EXPECT_TRUE(IsAligned(1, 1));
  EXPECT_TRUE(IsAligned(0xffff'ffff'ffff'ffff, 1));

  // Unaligned values.
  EXPECT_FALSE(IsAligned(1, 2));
  EXPECT_FALSE(IsAligned(0xffff'ffff'ffff'ffff, 2));
  EXPECT_FALSE(IsAligned(1, 0x8000'0000'0000'0000));
  EXPECT_FALSE(IsAligned(0xffff'ffff'ffff'ffff, 0x8000'0000'0000'0000));

  // Other aligned values.
  EXPECT_TRUE(IsAligned(0x8000'0000'0000'0000, 0x8000'0000'0000'0000));
  EXPECT_TRUE(IsAligned(0x4, 0x4));
  EXPECT_TRUE(IsAligned(0x40, 0x4));
}

}  // namespace
}  // namespace page_table::internal
