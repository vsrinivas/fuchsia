// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbl/confine_array_index.h"

#include <zxtest/zxtest.h>

namespace {

TEST(ConfineArrayIndexTest, ConfineArrayIndexTest) {
  constexpr size_t kLimit = 265;

  for (unsigned int i = 0; i < kLimit; i++) {
    EXPECT_EQ(i, fbl::confine_array_index(i, kLimit));
  }

  for (unsigned int i = kLimit; i < kLimit * 1.5; i++) {
    EXPECT_EQ(0, fbl::confine_array_index(i, kLimit));
  }

  EXPECT_EQ(0, fbl::confine_array_index(-1, kLimit));

  EXPECT_EQ(0, fbl::confine_array_index(-2, /*size=*/1));

  EXPECT_EQ(0, fbl::confine_array_index(0, /*size=*/1));
}

}  // namespace
