// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/common.h>

#include <zxtest/zxtest.h>

namespace {

TEST(Common, IncrementReset) {
  EXPECT_EQ(0u, inspect_counter_increment(kUniqueNameCounterId));
  EXPECT_EQ(1u, inspect_counter_increment(kUniqueNameCounterId));
  EXPECT_EQ(2u, inspect_counter_increment(kUniqueNameCounterId));
  EXPECT_EQ(3u, inspect_counter_increment(kUniqueNameCounterId));
  EXPECT_EQ(4u, inspect_counter_increment(kUniqueNameCounterId));
  EXPECT_EQ(5u, inspect_counter_increment(kUniqueNameCounterId));
  inspect_counter_reset(kUniqueNameCounterId);
  EXPECT_EQ(0u, inspect_counter_increment(kUniqueNameCounterId));
}

}  // namespace
