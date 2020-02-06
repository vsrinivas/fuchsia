// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "fbl/conditional_select_nospec.h"

namespace {

TEST(ConditionalSelectNoSpecTest, ConditionalSelectNospecTest) {
  EXPECT_EQ(1, fbl::conditional_select_nospec_eq(false, true, 0, 1));
  EXPECT_EQ(0, fbl::conditional_select_nospec_eq(true, true, 0, 1));
  EXPECT_EQ(6, fbl::conditional_select_nospec_eq(true, true, 6, 1));
  EXPECT_EQ(1, fbl::conditional_select_nospec_eq(66, 66, 1, 0));
  EXPECT_EQ(0, fbl::conditional_select_nospec_eq(67, 66, 1, 0));

  EXPECT_EQ(1, fbl::conditional_select_nospec_lt(65, 66, 1, 0));
  EXPECT_EQ(0, fbl::conditional_select_nospec_lt(66, 66, 1, 0));
  EXPECT_EQ(0, fbl::conditional_select_nospec_lt(67, 66, 1, 0));
  EXPECT_EQ(0, fbl::conditional_select_nospec_lt(-1, 66, 1, 0));
}

}  // namespace
