// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/crash_counter.h"

#include <gtest/gtest.h>

#include "src/developer/forensics/exceptions/tests/crasher_wrapper.h"
#include "src/developer/forensics/testing/gmatchers.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"

namespace forensics {
namespace exceptions {
namespace {

using inspect::testing::ChildrenMatch;
using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::UintIs;
using testing::UnorderedElementsAreArray;

using CrashCounterTest = UnitTestFixture;

TEST_F(CrashCounterTest, CrashCounts) {
  CrashCounter crash_counter(&InspectRoot());
  EXPECT_THAT(InspectTree(), ChildrenMatch(UnorderedElementsAreArray({
                                 NodeMatches(AllOf(NameMatches("crash_counts"))),
                             })));

  crash_counter.Increment("foo/bar/component");
  crash_counter.Increment("foo/bar/component");
  EXPECT_THAT(InspectTree(), ChildrenMatch(UnorderedElementsAreArray({
                                 AllOf(NodeMatches(AllOf(NameMatches("crash_counts"),
                                                         PropertyList(UnorderedElementsAreArray({
                                                             UintIs("foo/bar/component", 2u),
                                                         }))))),
                             })));

  crash_counter.Increment("baz/component");
  EXPECT_THAT(InspectTree(), ChildrenMatch(UnorderedElementsAreArray({
                                 AllOf(NodeMatches(AllOf(NameMatches("crash_counts"),
                                                         PropertyList(UnorderedElementsAreArray({
                                                             UintIs("foo/bar/component", 2u),
                                                             UintIs("baz/component", 1u),
                                                         }))))),
                             })));
}

}  // namespace
}  // namespace exceptions
}  // namespace forensics
