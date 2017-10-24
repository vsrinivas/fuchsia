// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "database.h"

#include "gtest/gtest.h"

namespace bluetooth {
namespace att {
namespace {

constexpr Handle kTestRangeStart = 1;
constexpr Handle kTestRangeEnd = 10;

constexpr common::UUID kTestType((uint16_t)0x2800);
const auto kTestValue = common::CreateStaticByteBuffer('t', 'e', 's', 't');

TEST(ATT_DatabaseTest, NewGroupingWhileEmptyError) {
  constexpr size_t kTooLarge = kTestRangeEnd - kTestRangeStart + 1;
  auto db = Database::Create(kTestRangeStart, kTestRangeEnd);
  EXPECT_FALSE(db->NewGrouping(kTestType, kTooLarge, kTestValue));
}

TEST(ATT_DatabaseTest, NewGroupingWhileEmptyFill) {
  constexpr size_t kExact = kTestRangeEnd - kTestRangeStart;

  auto db = Database::Create(kTestRangeStart, kTestRangeEnd);
  auto* grp = db->NewGrouping(kTestType, kExact, kTestValue);
  ASSERT_TRUE(grp);
  EXPECT_EQ(kTestType, grp->group_type());
  EXPECT_EQ(kTestRangeStart, grp->start_handle());
  EXPECT_EQ(kTestRangeEnd, grp->end_handle());

  // Ran out of space.
  EXPECT_FALSE(db->NewGrouping(kTestType, 0, kTestValue));
}

// This test case performs multiple insertions and removals on the same
// database.
TEST(ATT_DatabaseTest, NewGroupingMultipleInsertions) {
  // [__________]
  auto db = Database::Create(kTestRangeStart, kTestRangeEnd);

  // Insert to empty db
  // [XXX_______] (insert X)
  auto* grp = db->NewGrouping(kTestType, 2, kTestValue);
  ASSERT_TRUE(grp);
  EXPECT_EQ(1, grp->start_handle());
  EXPECT_EQ(3, grp->end_handle());

  // Not enough space
  grp = db->NewGrouping(kTestType, 7, kTestValue);
  EXPECT_FALSE(grp);

  // Insert back
  // [XXXYYYYY__] (insert Y)
  grp = db->NewGrouping(kTestType, 4, kTestValue);
  ASSERT_TRUE(grp);
  EXPECT_EQ(4, grp->start_handle());
  EXPECT_EQ(8, grp->end_handle());

  // Not enough space
  grp = db->NewGrouping(kTestType, 2, kTestValue);
  EXPECT_FALSE(grp);

  // Insert back
  // [XXXYYYYYZZ] (insert Z)
  grp = db->NewGrouping(kTestType, 1, kTestValue);
  ASSERT_TRUE(grp);
  EXPECT_EQ(9, grp->start_handle());
  EXPECT_EQ(10, grp->end_handle());

  // Out of space
  EXPECT_FALSE(db->NewGrouping(kTestType, 0, kTestValue));

  // Remove first grouping. It should be possible to reinsert a smaller group.
  // [___YYYYYZZ]
  EXPECT_TRUE(db->RemoveGrouping(1));

  // Not enough space
  grp = db->NewGrouping(kTestType, 3, kTestValue);
  EXPECT_FALSE(grp);

  // Insert front
  // [XX_YYYYYZZ] (insert X)
  grp = db->NewGrouping(kTestType, 1, kTestValue);
  ASSERT_TRUE(grp);
  EXPECT_EQ(1, grp->start_handle());
  EXPECT_EQ(2, grp->end_handle());

  // Handle doesn't exist.
  EXPECT_FALSE(db->RemoveGrouping(3));

  // Insert in the middle
  // [XXWYYYYYZZ] (insert W)
  grp = db->NewGrouping(kTestType, 0, kTestValue);
  ASSERT_TRUE(grp);
  EXPECT_EQ(3, grp->start_handle());
  EXPECT_EQ(3, grp->end_handle());

  // [XXW_____ZZ] (remove Y)
  EXPECT_TRUE(db->RemoveGrouping(4));

  // Insert in the middle
  // [XXWAAA__ZZ] (insert A)
  grp = db->NewGrouping(kTestType, 2, kTestValue);
  ASSERT_TRUE(grp);
  EXPECT_EQ(4, grp->start_handle());
  EXPECT_EQ(6, grp->end_handle());

  // Insert in the middle
  // [XXWAAABBZZ] (insert B)
  grp = db->NewGrouping(kTestType, 1, kTestValue);
  ASSERT_TRUE(grp);
  EXPECT_EQ(7, grp->start_handle());
  EXPECT_EQ(8, grp->end_handle());

  // Out of space
  EXPECT_FALSE(db->NewGrouping(kTestType, 0, kTestValue));
}

TEST(ATT_DatabaseTest, RemoveWhileEmpty) {
  auto db = Database::Create(kTestRangeStart, kTestRangeEnd);
  EXPECT_FALSE(db->RemoveGrouping(kTestRangeStart));
}

}  // namespace
}  // namespace att
}  // namespace bluetooth
