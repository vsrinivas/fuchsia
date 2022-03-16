// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/redact/cache.h"

#include <lib/inspect/testing/cpp/inspect.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/testing/unit_test_fixture.h"

namespace forensics {
namespace {

using inspect::testing::ChildrenMatch;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::UintIs;
using testing::AllOf;
using testing::ElementsAreArray;

using RedactionIdCacheTest = UnitTestFixture;

TEST_F(RedactionIdCacheTest, GetId) {
  RedactionIdCache cache(InspectRoot().CreateUint("size", 0u));
  EXPECT_EQ(cache.GetId("value1"), 1);
  EXPECT_EQ(cache.GetId("value1"), 1);
  EXPECT_EQ(cache.GetId("value1"), 1);
  EXPECT_THAT(InspectTree(), NodeMatches(AllOf(PropertyList(ElementsAreArray({
                                 UintIs("size", 1u),
                             })))));

  EXPECT_EQ(cache.GetId("value2"), 2);
  EXPECT_EQ(cache.GetId("value2"), 2);
  EXPECT_EQ(cache.GetId("value2"), 2);
  EXPECT_THAT(InspectTree(), NodeMatches(AllOf(PropertyList(ElementsAreArray({
                                 UintIs("size", 2u),
                             })))));

  EXPECT_EQ(cache.GetId("value3"), 3);
  EXPECT_EQ(cache.GetId("value3"), 3);
  EXPECT_EQ(cache.GetId("value3"), 3);
  EXPECT_THAT(InspectTree(), NodeMatches(AllOf(PropertyList(ElementsAreArray({
                                 UintIs("size", 3u),
                             })))));

  EXPECT_EQ(cache.GetId("value4"), 4);
  EXPECT_EQ(cache.GetId("value4"), 4);
  EXPECT_EQ(cache.GetId("value4"), 4);
  EXPECT_THAT(InspectTree(), NodeMatches(AllOf(PropertyList(ElementsAreArray({
                                 UintIs("size", 4u),
                             })))));
}

TEST_F(RedactionIdCacheTest, StartingId) {
  RedactionIdCache cache(InspectRoot().CreateUint("size", 0u), 100);
  EXPECT_EQ(cache.GetId("value1"), 101);
  EXPECT_EQ(cache.GetId("value1"), 101);
  EXPECT_EQ(cache.GetId("value1"), 101);
  EXPECT_THAT(InspectTree(), NodeMatches(AllOf(PropertyList(ElementsAreArray({
                                 UintIs("size", 1u),
                             })))));

  EXPECT_EQ(cache.GetId("value2"), 102);
  EXPECT_EQ(cache.GetId("value2"), 102);
  EXPECT_EQ(cache.GetId("value2"), 102);
  EXPECT_THAT(InspectTree(), NodeMatches(AllOf(PropertyList(ElementsAreArray({
                                 UintIs("size", 2u),
                             })))));

  EXPECT_EQ(cache.GetId("value3"), 103);
  EXPECT_EQ(cache.GetId("value3"), 103);
  EXPECT_EQ(cache.GetId("value3"), 103);
  EXPECT_THAT(InspectTree(), NodeMatches(AllOf(PropertyList(ElementsAreArray({
                                 UintIs("size", 3u),
                             })))));

  EXPECT_EQ(cache.GetId("value4"), 104);
  EXPECT_EQ(cache.GetId("value4"), 104);
  EXPECT_EQ(cache.GetId("value4"), 104);
  EXPECT_THAT(InspectTree(), NodeMatches(AllOf(PropertyList(ElementsAreArray({
                                 UintIs("size", 4u),
                             })))));
}

}  // namespace
}  // namespace forensics
