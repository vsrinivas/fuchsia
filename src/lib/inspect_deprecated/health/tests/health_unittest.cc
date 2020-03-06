// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/inspect_deprecated/health/health.h"

#include <abs_clock/clock.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/inspect_deprecated/reader.h"
#include "src/lib/inspect_deprecated/testing/inspect.h"

using namespace inspect_deprecated::testing;
using testing::ElementsAre;
using testing::IsEmpty;
using testing::UnorderedElementsAre;

namespace {

TEST(InspectHealth, Default) {
  abs_clock::FakeClock clock;
  clock.AdvanceTime(zx::nsec(42));

  auto tree = inspect_deprecated::Inspector().CreateTree();
  auto health = inspect_deprecated::NodeHealth(&tree.GetRoot(), &clock);

  auto hierarchy = inspect_deprecated::ReadFromVmo(tree.DuplicateVmo()).take_value();
  auto* node = hierarchy.GetByPath({inspect_deprecated::kHealthNodeName});
  ASSERT_TRUE(node != nullptr);
  EXPECT_THAT(*node, NodeMatches(AllOf(NameMatches(inspect_deprecated::kHealthNodeName),
                                       PropertyList(UnorderedElementsAre(StringPropertyIs(
                                           "status", inspect_deprecated::kHealthStartingUp))),
                                       MetricList(UnorderedElementsAre(IntMetricIs(
                                           inspect_deprecated::kStartTimestamp, 42))))));
}

TEST(InspectHealth, Ok) {
  auto tree = inspect_deprecated::Inspector().CreateTree();
  auto health = inspect_deprecated::NodeHealth(&tree.GetRoot());
  health.Ok();

  auto hierarchy = inspect_deprecated::ReadFromVmo(tree.DuplicateVmo()).take_value();
  auto* node = hierarchy.GetByPath({inspect_deprecated::kHealthNodeName});
  ASSERT_TRUE(node != nullptr);
  EXPECT_THAT(*node, NodeMatches(AllOf(NameMatches(inspect_deprecated::kHealthNodeName),
                                       PropertyList(UnorderedElementsAre(StringPropertyIs(
                                           "status", inspect_deprecated::kHealthOk))))));
}

TEST(InspectHealth, UnhealthyToStartingUp) {
  auto tree = inspect_deprecated::Inspector().CreateTree();
  auto health = inspect_deprecated::NodeHealth(&tree.GetRoot());
  health.Unhealthy("test");
  health.StartingUp();

  auto hierarchy = inspect_deprecated::ReadFromVmo(tree.DuplicateVmo()).take_value();
  auto* node = hierarchy.GetByPath({inspect_deprecated::kHealthNodeName});
  ASSERT_TRUE(node != nullptr);
  EXPECT_THAT(*node, NodeMatches(AllOf(NameMatches(inspect_deprecated::kHealthNodeName),
                                       PropertyList(UnorderedElementsAre(StringPropertyIs(
                                           "status", inspect_deprecated::kHealthStartingUp))))));
}

TEST(InspectHealth, Unhealthy) {
  auto tree = inspect_deprecated::Inspector().CreateTree();
  auto health = inspect_deprecated::NodeHealth(&tree.GetRoot());
  health.Unhealthy("test");

  auto hierarchy = inspect_deprecated::ReadFromVmo(tree.DuplicateVmo()).take_value();
  auto* node = hierarchy.GetByPath({inspect_deprecated::kHealthNodeName});
  ASSERT_TRUE(node != nullptr);
  EXPECT_THAT(
      *node, NodeMatches(AllOf(NameMatches(inspect_deprecated::kHealthNodeName),
                               PropertyList(UnorderedElementsAre(
                                   StringPropertyIs("status", inspect_deprecated::kHealthUnhealthy),
                                   StringPropertyIs("message", "test"))))));
}

TEST(InspectHealth, StartingUpReason) {
  auto tree = inspect_deprecated::Inspector().CreateTree();
  auto health = inspect_deprecated::NodeHealth(&tree.GetRoot());
  health.StartingUp("test");

  auto hierarchy = inspect_deprecated::ReadFromVmo(tree.DuplicateVmo()).take_value();
  auto* node = hierarchy.GetByPath({inspect_deprecated::kHealthNodeName});
  ASSERT_TRUE(node != nullptr);
  EXPECT_THAT(*node, NodeMatches(AllOf(
                         NameMatches(inspect_deprecated::kHealthNodeName),
                         PropertyList(UnorderedElementsAre(
                             StringPropertyIs("status", inspect_deprecated::kHealthStartingUp),
                             StringPropertyIs("message", "test"))))));
}

TEST(InspectHealth, CustomMessage) {
  auto tree = inspect_deprecated::Inspector().CreateTree();
  auto health = inspect_deprecated::NodeHealth(&tree.GetRoot());
  health.SetStatus("BAD CONFIG", "test");

  auto hierarchy = inspect_deprecated::ReadFromVmo(tree.DuplicateVmo()).take_value();
  auto* node = hierarchy.GetByPath({inspect_deprecated::kHealthNodeName});
  ASSERT_TRUE(node != nullptr);
  EXPECT_THAT(*node, NodeMatches(AllOf(
                         NameMatches(inspect_deprecated::kHealthNodeName),
                         PropertyList(UnorderedElementsAre(StringPropertyIs("status", "BAD CONFIG"),
                                                           StringPropertyIs("message", "test"))))));
}

}  // namespace
