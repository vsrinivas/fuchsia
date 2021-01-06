// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/address_ranges.h"

#include <gtest/gtest.h>

namespace zxdb {

TEST(AddressRanges, Basic) {
  AddressRanges a;
  EXPECT_TRUE(a.empty());

  // Empty input range should be deleted.
  AddressRanges b(AddressRange(5, 5));
  EXPECT_TRUE(b.empty());
}

TEST(AddressRanges, NonCanonical) {
  AddressRanges a(AddressRanges::kNonCanonical, {});
  EXPECT_TRUE(a.empty());
  EXPECT_EQ("{}", a.ToString());
  EXPECT_EQ(AddressRange(), a.GetExtent());

  AddressRanges b(AddressRanges::kNonCanonical, {AddressRange(0, 0)});
  EXPECT_TRUE(b.empty());
  EXPECT_EQ(AddressRange(0, 0), b.GetExtent());

  // Enclosed inputs.
  AddressRanges c(
      AddressRanges::kNonCanonical,
      {AddressRange(0x100, 0x200), AddressRange(0x110, 0x120), AddressRange(0x140, 0x150)});
  ASSERT_EQ(1u, c.size());
  EXPECT_EQ(AddressRange(0x100, 0x200), c[0]);
  EXPECT_EQ("{[0x100, 0x200)}", c.ToString());
  EXPECT_EQ(AddressRange(0x100, 0x200), c.GetExtent());

  // Overlapping inputs.
  AddressRanges d(AddressRanges::kNonCanonical,
                  {AddressRange(0x100, 0x200), AddressRange(0x150, 0x300),
                   AddressRange(0x250, 0x400), AddressRange(0x500, 0x600)});
  ASSERT_EQ(2u, d.size());
  EXPECT_EQ(AddressRange(0x100, 0x400), d[0]);
  EXPECT_EQ(AddressRange(0x500, 0x600), d[1]);
  EXPECT_EQ("{[0x100, 0x400), [0x500, 0x600)}", d.ToString());

  // Non-sorted and overlapping.
  AddressRanges e(AddressRanges::kNonCanonical,
                  {AddressRange(0x500, 0x600), AddressRange(0x100, 0x200),
                   AddressRange(0x150, 0x300), AddressRange(0x250, 0x400)});
  ASSERT_EQ(2u, e.size());
  EXPECT_EQ(AddressRange(0x100, 0x400), e[0]);
  EXPECT_EQ(AddressRange(0x500, 0x600), e[1]);
  EXPECT_EQ(AddressRange(0x100, 0x600), e.GetExtent());
}

TEST(AddressRanges, GetRangeContaining) {
  AddressRanges empty;
  EXPECT_FALSE(empty.GetRangeContaining(0x123));

  // This has two touching ranges to test the boundary condition, and one by itself.
  AddressRanges some(AddressRanges::kCanonical,
                     {AddressRange(100, 200), AddressRange(200, 300), AddressRange(400, 500)});

  EXPECT_FALSE(some.GetRangeContaining(99));

  std::optional<AddressRange> result = some.GetRangeContaining(100);
  ASSERT_TRUE(result);
  ASSERT_EQ(some[0], *result);

  result = some.GetRangeContaining(150);
  ASSERT_TRUE(result);
  ASSERT_EQ(some[0], *result);

  // Ends are non-inclusive, so the boundary should be in the second one.
  result = some.GetRangeContaining(200);
  ASSERT_TRUE(result);
  EXPECT_EQ(some[1], *result);

  EXPECT_FALSE(some.GetRangeContaining(300));  // Non-inclusive end of the last range.

  result = some.GetRangeContaining(400);
  ASSERT_TRUE(result);
  EXPECT_EQ(some[2], *result);
}

TEST(AddressRanges, InRange) {
  AddressRanges empty;
  EXPECT_FALSE(empty.InRange(0));

  AddressRanges one(AddressRanges::kCanonical, {AddressRange(100, 200)});
  EXPECT_FALSE(one.InRange(99));
  EXPECT_TRUE(one.InRange(100));
  EXPECT_TRUE(one.InRange(199));
  EXPECT_FALSE(one.InRange(200));
  EXPECT_FALSE(one.InRange(300));

  AddressRanges two(AddressRanges::kCanonical, {AddressRange(100, 200), AddressRange(300, 400)});
  EXPECT_FALSE(two.InRange(0));
  EXPECT_FALSE(two.InRange(99));
  EXPECT_TRUE(two.InRange(100));
  EXPECT_TRUE(two.InRange(199));
  EXPECT_FALSE(two.InRange(200));
  EXPECT_FALSE(two.InRange(299));
  EXPECT_TRUE(two.InRange(300));
  EXPECT_TRUE(two.InRange(399));
  EXPECT_FALSE(two.InRange(400));
  EXPECT_FALSE(two.InRange(499));
}

}  // namespace zxdb
