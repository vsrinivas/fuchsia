// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/address_range.h"

#include "gtest/gtest.h"

namespace debug_ipc {

TEST(AddressRange, InRange) {
  constexpr AddressRange range(1, 5);
  EXPECT_FALSE(range.InRange(0));
  EXPECT_TRUE(range.InRange(1));
  EXPECT_TRUE(range.InRange(4));
  EXPECT_FALSE(range.InRange(5));
}

TEST(AddressRange, Contains) {
  constexpr AddressRange range(100, 105);

  // A range can contain itself.
  EXPECT_TRUE(range.Contains(range));

  // Completely inside.
  EXPECT_TRUE(range.Contains(AddressRange(102, 104)));

  // Completely outside.
  EXPECT_FALSE(range.Contains(AddressRange(1, 99)));
  EXPECT_FALSE(range.Contains(AddressRange(200, 205)));

  // Partially overlapping.
  EXPECT_FALSE(range.Contains(AddressRange(0, 102)));
  EXPECT_FALSE(range.Contains(AddressRange(102, 200)));
}

TEST(AddressRange, Overlaps) {
  constexpr AddressRange range(100, 105);

  // A range can contain itself.
  EXPECT_TRUE(range.Overlaps(range));

  // Completely inside.
  EXPECT_TRUE(range.Overlaps(AddressRange(102, 104)));

  // Completely outside.
  EXPECT_FALSE(range.Overlaps(AddressRange(1, 99)));
  EXPECT_FALSE(range.Overlaps(AddressRange(200, 205)));

  // Partially overlapping.
  EXPECT_TRUE(range.Overlaps(AddressRange(0, 102)));
  EXPECT_TRUE(range.Overlaps(AddressRange(102, 200)));
}

TEST(AddressRange, Union) {
  constexpr AddressRange range(100, 105);
  constexpr AddressRange empty;

  // Union with itself.
  EXPECT_EQ(range, range.Union(range));

  // Union with empty. Shouldn't matter where the empty range is. Check both sides being empty.
  EXPECT_EQ(range, range.Union(empty));
  EXPECT_EQ(range, empty.Union(range));
  EXPECT_EQ(range, range.Union(AddressRange(1000, 1000)));
  EXPECT_EQ(range, AddressRange(1000, 1000).Union(range));

  // Completely inside.
  EXPECT_EQ(range, range.Union(AddressRange(102, 104)));

  // Completely outside.
  EXPECT_EQ(AddressRange(1, 105), range.Union(AddressRange(1, 99)));
  EXPECT_EQ(AddressRange(100, 205), range.Union(AddressRange(200, 205)));

  // Partially overlapping.
  EXPECT_EQ(AddressRange(0, 105), range.Union(AddressRange(0, 102)));
  EXPECT_EQ(AddressRange(100, 200), range.Union(AddressRange(102, 200)));
}

}  // namespace debug_ipc
