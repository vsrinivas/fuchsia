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

constexpr uint16_t kDefaultMTU = 23;

constexpr common::UUID kTestType1((uint16_t)1);
constexpr common::UUID kTestType2((uint16_t)2);

// Values with different lengths
const auto kTestValue1 = common::CreateStaticByteBuffer('x', 'x');
const auto kTestValue2 = common::CreateStaticByteBuffer('x', 'x', 'x');

TEST(ATT_DatabaseTest, NewGroupingWhileEmptyError) {
  constexpr size_t kTooLarge = kTestRangeEnd - kTestRangeStart + 1;
  auto db = Database::Create(kTestRangeStart, kTestRangeEnd);
  EXPECT_FALSE(db->NewGrouping(kTestType1, kTooLarge, kTestValue1));
}

TEST(ATT_DatabaseTest, NewGroupingWhileEmptyFill) {
  constexpr size_t kExact = kTestRangeEnd - kTestRangeStart;

  auto db = Database::Create(kTestRangeStart, kTestRangeEnd);
  auto* grp = db->NewGrouping(kTestType1, kExact, kTestValue1);
  ASSERT_TRUE(grp);
  EXPECT_EQ(kTestType1, grp->group_type());
  EXPECT_EQ(kTestRangeStart, grp->start_handle());
  EXPECT_EQ(kTestRangeEnd, grp->end_handle());

  // Ran out of space.
  EXPECT_FALSE(db->NewGrouping(kTestType1, 0, kTestValue1));
}

// This test case performs multiple insertions and removals on the same
// database.
TEST(ATT_DatabaseTest, NewGroupingMultipleInsertions) {
  // [__________]
  auto db = Database::Create(kTestRangeStart, kTestRangeEnd);

  // Insert to empty db
  // [XXX_______] (insert X)
  auto* grp = db->NewGrouping(kTestType1, 2, kTestValue1);
  ASSERT_TRUE(grp);
  EXPECT_EQ(1, grp->start_handle());
  EXPECT_EQ(3, grp->end_handle());

  // Not enough space
  grp = db->NewGrouping(kTestType1, 7, kTestValue1);
  EXPECT_FALSE(grp);

  // Insert back
  // [XXXYYYYY__] (insert Y)
  grp = db->NewGrouping(kTestType1, 4, kTestValue1);
  ASSERT_TRUE(grp);
  EXPECT_EQ(4, grp->start_handle());
  EXPECT_EQ(8, grp->end_handle());

  // Not enough space
  grp = db->NewGrouping(kTestType1, 2, kTestValue1);
  EXPECT_FALSE(grp);

  // Insert back
  // [XXXYYYYYZZ] (insert Z)
  grp = db->NewGrouping(kTestType1, 1, kTestValue1);
  ASSERT_TRUE(grp);
  EXPECT_EQ(9, grp->start_handle());
  EXPECT_EQ(10, grp->end_handle());

  // Out of space
  EXPECT_FALSE(db->NewGrouping(kTestType1, 0, kTestValue1));

  // Remove first grouping. It should be possible to reinsert a smaller group.
  // [___YYYYYZZ]
  EXPECT_TRUE(db->RemoveGrouping(1));

  // Not enough space
  grp = db->NewGrouping(kTestType1, 3, kTestValue1);
  EXPECT_FALSE(grp);

  // Insert front
  // [XX_YYYYYZZ] (insert X)
  grp = db->NewGrouping(kTestType1, 1, kTestValue1);
  ASSERT_TRUE(grp);
  EXPECT_EQ(1, grp->start_handle());
  EXPECT_EQ(2, grp->end_handle());

  // Handle doesn't exist.
  EXPECT_FALSE(db->RemoveGrouping(3));

  // Insert in the middle
  // [XXWYYYYYZZ] (insert W)
  grp = db->NewGrouping(kTestType1, 0, kTestValue1);
  ASSERT_TRUE(grp);
  EXPECT_EQ(3, grp->start_handle());
  EXPECT_EQ(3, grp->end_handle());

  // [XXW_____ZZ] (remove Y)
  EXPECT_TRUE(db->RemoveGrouping(4));

  // Insert in the middle
  // [XXWAAA__ZZ] (insert A)
  grp = db->NewGrouping(kTestType1, 2, kTestValue1);
  ASSERT_TRUE(grp);
  EXPECT_EQ(4, grp->start_handle());
  EXPECT_EQ(6, grp->end_handle());

  // Insert in the middle
  // [XXWAAABBZZ] (insert B)
  grp = db->NewGrouping(kTestType1, 1, kTestValue1);
  ASSERT_TRUE(grp);
  EXPECT_EQ(7, grp->start_handle());
  EXPECT_EQ(8, grp->end_handle());

  // Out of space
  EXPECT_FALSE(db->NewGrouping(kTestType1, 0, kTestValue1));
}

TEST(ATT_DatabaseTest, RemoveWhileEmpty) {
  auto db = Database::Create(kTestRangeStart, kTestRangeEnd);
  EXPECT_FALSE(db->RemoveGrouping(kTestRangeStart));
}

TEST(ATT_DatabaseTest, ReadByGroupTypeInvalidHandle) {
  auto db = Database::Create(kTestRangeStart, kTestRangeEnd);

  std::list<AttributeGrouping*> results;

  // Handle is 0.
  EXPECT_EQ(ErrorCode::kInvalidHandle,
            db->ReadByGroupType(kInvalidHandle, kTestRangeEnd, kTestType1,
                                kDefaultMTU, &results));
  EXPECT_EQ(ErrorCode::kInvalidHandle,
            db->ReadByGroupType(kTestRangeStart, kInvalidHandle, kTestType1,
                                kDefaultMTU, &results));

  // end > start
  EXPECT_EQ(ErrorCode::kInvalidHandle,
            db->ReadByGroupType(kTestRangeStart + 1, kTestRangeStart,
                                kTestType1, kDefaultMTU, &results));
}

TEST(ATT_DatabaseTest, ReadByGroupTypeEmpty) {
  auto db = Database::Create(kTestRangeStart, kTestRangeEnd);

  std::list<AttributeGrouping*> results;

  EXPECT_EQ(ErrorCode::kAttributeNotFound,
            db->ReadByGroupType(kTestRangeStart, kTestRangeEnd, kTestType1,
                                kDefaultMTU, &results));
}

TEST(ATT_DatabaseTest, ReadByGroupTypeOutOfRange) {
  constexpr size_t kPadding = 3;
  auto db = Database::Create(kTestRangeStart, kTestRangeEnd);

  db->NewGrouping(kTestType1, kPadding, kTestValue1);
  auto* grp = db->NewGrouping(kTestType2, 0, kTestValue1);
  grp->set_active(true);

  std::list<AttributeGrouping*> results;

  // Search before
  EXPECT_EQ(ErrorCode::kAttributeNotFound,
            db->ReadByGroupType(kTestRangeStart, grp->start_handle() - 1,
                                kTestType2, kDefaultMTU, &results));

  // Search after
  EXPECT_EQ(ErrorCode::kAttributeNotFound,
            db->ReadByGroupType(grp->end_handle() + 1, kTestRangeEnd,
                                kTestType2, kDefaultMTU, &results));
}

TEST(ATT_DatabaseTest, ReadByGroupTypeIncomplete) {
  auto db = Database::Create(kTestRangeStart, kTestRangeEnd);

  std::list<AttributeGrouping*> results;
  db->NewGrouping(kTestType1, 2, kTestValue1);

  EXPECT_EQ(ErrorCode::kAttributeNotFound,
            db->ReadByGroupType(kTestRangeStart, kTestRangeEnd, kTestType1,
                                kDefaultMTU, &results));
}

TEST(ATT_DatabaseTest, ReadByGroupTypeInactive) {
  auto db = Database::Create(kTestRangeStart, kTestRangeEnd);

  std::list<AttributeGrouping*> results;

  // Complete but inactive
  auto* grp = db->NewGrouping(kTestType1, 0, kTestValue1);
  ASSERT_FALSE(grp->active());

  EXPECT_EQ(ErrorCode::kAttributeNotFound,
            db->ReadByGroupType(kTestRangeStart, kTestRangeEnd, kTestType1,
                                kDefaultMTU, &results));
}

TEST(ATT_DatabaseTest, ReadByGroupTypeSingle) {
  auto db = Database::Create(kTestRangeStart, kTestRangeEnd);

  std::list<AttributeGrouping*> results;

  auto* grp = db->NewGrouping(kTestType1, 0, kTestValue1);
  grp->set_active(true);

  EXPECT_EQ(ErrorCode::kNoError,
            db->ReadByGroupType(kTestRangeStart, kTestRangeEnd, kTestType1,
                                kDefaultMTU, &results));
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(grp->start_handle(), results.front()->start_handle());
  EXPECT_EQ(grp->end_handle(), results.front()->end_handle());
  EXPECT_EQ(kTestType1, results.front()->group_type());
}

TEST(ATT_DatabaseTest, ReadByGroupTypeMultipleSameValueBasic) {
  auto db = Database::Create(kTestRangeStart, kTestRangeEnd);

  // Match
  auto* grp = db->NewGrouping(kTestType1, 0, kTestValue1);
  grp->set_active(true);
  Handle match_handle1 = grp->start_handle();

  // No match
  grp = db->NewGrouping(kTestType2, 0, kTestValue1);
  grp->set_active(true);

  // Match
  grp = db->NewGrouping(kTestType1, 0, kTestValue1);
  grp->set_active(true);
  Handle match_handle2 = grp->start_handle();

  std::list<AttributeGrouping*> results;
  EXPECT_EQ(ErrorCode::kNoError,
            db->ReadByGroupType(kTestRangeStart, kTestRangeEnd, kTestType1,
                                kDefaultMTU, &results));

  ASSERT_EQ(2u, results.size());
  EXPECT_EQ(match_handle1, results.front()->start_handle());
  EXPECT_EQ(kTestType1, results.front()->group_type());
  EXPECT_EQ(match_handle2, results.back()->start_handle());
  EXPECT_EQ(kTestType1, results.back()->group_type());
}

TEST(ATT_DatabaseTest, ReadByGroupTypeNarrowerRange) {
  auto db = Database::Create(kTestRangeStart, kTestRangeEnd);

  // Insert 10 matching results
  for (Handle h = kTestRangeStart; h <= kTestRangeEnd; ++h) {
    auto* grp = db->NewGrouping(kTestType1, 0, kTestValue1);
    grp->set_active(true);
  }

  constexpr Handle kStart = 5;
  constexpr Handle kEnd = 8;
  constexpr size_t kExpectedCount = kEnd - kStart + 1;

  // Make this large enough to hold kExpectedCount results.
  const uint16_t kMTU =
      (kTestValue1.size() + sizeof(AttributeGroupDataEntry)) * kExpectedCount;

  std::list<AttributeGrouping*> results;
  EXPECT_EQ(ErrorCode::kNoError,
            db->ReadByGroupType(kStart, kEnd, kTestType1, kMTU, &results));
  ASSERT_EQ(kExpectedCount, results.size());

  for (Handle h = kStart; h <= kEnd; ++h) {
    ASSERT_TRUE(!results.empty());
    EXPECT_EQ(h, results.front()->start_handle());
    results.pop_front();
  }

  // Search for the last handle only. This should return the last group.
  EXPECT_EQ(ErrorCode::kNoError,
            db->ReadByGroupType(kTestRangeEnd, kTestRangeEnd, kTestType1, kMTU,
                                &results));
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(kTestRangeEnd, results.front()->start_handle());
}

TEST(ATT_DatabaseTest, ReadByGroupTypeVaryingLengths) {
  auto db = Database::Create(kTestRangeStart, kTestRangeEnd);

  auto* grp = db->NewGrouping(kTestType1, 0, kTestValue1);
  grp->set_active(true);
  Handle match_handle = grp->start_handle();

  // Matching type but value of different size. The results will stop here.
  grp = db->NewGrouping(kTestType1, 0, kTestValue2);
  grp->set_active(true);

  // Matching type and matching value length. This won't be included as the
  // request will terminate at the second attribute.
  grp = db->NewGrouping(kTestType1, 0, kTestValue1);
  grp->set_active(true);

  std::list<AttributeGrouping*> results;
  EXPECT_EQ(ErrorCode::kNoError,
            db->ReadByGroupType(kTestRangeStart, kTestRangeEnd, kTestType1,
                                kDefaultMTU, &results));

  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(match_handle, results.front()->start_handle());
}

TEST(ATT_DatabaseTest, ReadByGroupTypeExceedsMTU) {
  auto db = Database::Create(kTestRangeStart, kTestRangeEnd);

  // Add two group entries of equal type and value length. The second one will
  // be omitted as it won't fit in the payload.
  auto* grp = db->NewGrouping(kTestType1, 0, kTestValue1);
  grp->set_active(true);
  Handle match_handle = grp->start_handle();

  grp = db->NewGrouping(kTestType1, 0, kTestValue1);
  grp->set_active(true);

  // Just one octet short.
  const uint16_t kMTU =
      (kTestValue1.size() + sizeof(AttributeGroupDataEntry)) * 2 - 1;

  std::list<AttributeGrouping*> results;
  EXPECT_EQ(ErrorCode::kNoError,
            db->ReadByGroupType(kTestRangeStart, kTestRangeEnd, kTestType1,
                                kMTU, &results));

  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(match_handle, results.front()->start_handle());
}

TEST(ATT_DatabaseTest, ReadByGroupTypeFirstValueExceedsMTU) {
  auto db = Database::Create(kTestRangeStart, kTestRangeEnd);

  // Add two group entries of equal type and value length.
  auto* grp = db->NewGrouping(kTestType1, 0, kTestValue1);
  grp->set_active(true);
  Handle match_handle = grp->start_handle();

  grp = db->NewGrouping(kTestType1, 0, kTestValue1);
  grp->set_active(true);

  // Pick an MTU that is just one octet short of accomodating the first entry.
  // The result should contain this entry regardless.
  const uint16_t kMTU =
      kTestValue1.size() + sizeof(AttributeGroupDataEntry) - 1;

  std::list<AttributeGrouping*> results;
  EXPECT_EQ(ErrorCode::kNoError,
            db->ReadByGroupType(kTestRangeStart, kTestRangeEnd, kTestType1,
                                kMTU, &results));

  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(match_handle, results.front()->start_handle());
}

}  // namespace
}  // namespace att
}  // namespace bluetooth
