// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect_deprecated/health/health.h>
#include <lib/inspect_deprecated/testing/inspect.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/inspect_deprecated/reader.h"

using namespace inspect_deprecated::testing;
using testing::ElementsAre;
using testing::IsEmpty;
using testing::UnorderedElementsAre;

namespace {

TEST(InspectHealth, Default) {
  auto tree = inspect_deprecated::Inspector().CreateTree("test");
  auto health = inspect_deprecated::NodeHealth(&tree.GetRoot());

  auto hierarchy = inspect_deprecated::ReadFromVmo(tree.GetVmo()).take_value();
  auto* node = hierarchy.GetByPath({inspect_deprecated::kHealthNodeName});
  ASSERT_TRUE(node != nullptr);
  EXPECT_THAT(*node, NodeMatches(AllOf(NameMatches(inspect_deprecated::kHealthNodeName),
                                       PropertyList(UnorderedElementsAre(StringPropertyIs(
                                           "status", inspect_deprecated::kHealthStartingUp))))));
}

TEST(InspectHealth, Ok) {
  auto tree = inspect_deprecated::Inspector().CreateTree("test");
  auto health = inspect_deprecated::NodeHealth(&tree.GetRoot());
  health.Ok();

  auto hierarchy = inspect_deprecated::ReadFromVmo(tree.GetVmo()).take_value();
  auto* node = hierarchy.GetByPath({inspect_deprecated::kHealthNodeName});
  ASSERT_TRUE(node != nullptr);
  EXPECT_THAT(*node, NodeMatches(AllOf(NameMatches(inspect_deprecated::kHealthNodeName),
                                       PropertyList(UnorderedElementsAre(StringPropertyIs(
                                           "status", inspect_deprecated::kHealthOk))))));
}

TEST(InspectHealth, UnhealthyToStartingUp) {
  auto tree = inspect_deprecated::Inspector().CreateTree("test");
  auto health = inspect_deprecated::NodeHealth(&tree.GetRoot());
  health.Unhealthy("test");
  health.StartingUp();

  auto hierarchy = inspect_deprecated::ReadFromVmo(tree.GetVmo()).take_value();
  auto* node = hierarchy.GetByPath({inspect_deprecated::kHealthNodeName});
  ASSERT_TRUE(node != nullptr);
  EXPECT_THAT(*node, NodeMatches(AllOf(NameMatches(inspect_deprecated::kHealthNodeName),
                                       PropertyList(UnorderedElementsAre(StringPropertyIs(
                                           "status", inspect_deprecated::kHealthStartingUp))))));
}

TEST(InspectHealth, Unhealthy) {
  auto tree = inspect_deprecated::Inspector().CreateTree("test");
  auto health = inspect_deprecated::NodeHealth(&tree.GetRoot());
  health.Unhealthy("test");

  auto hierarchy = inspect_deprecated::ReadFromVmo(tree.GetVmo()).take_value();
  auto* node = hierarchy.GetByPath({inspect_deprecated::kHealthNodeName});
  ASSERT_TRUE(node != nullptr);
  EXPECT_THAT(
      *node, NodeMatches(AllOf(NameMatches(inspect_deprecated::kHealthNodeName),
                               PropertyList(UnorderedElementsAre(
                                   StringPropertyIs("status", inspect_deprecated::kHealthUnhealthy),
                                   StringPropertyIs("message", "test"))))));
}

TEST(InspectHealth, StartingUpReason) {
  auto tree = inspect_deprecated::Inspector().CreateTree("test");
  auto health = inspect_deprecated::NodeHealth(&tree.GetRoot());
  health.StartingUp("test");

  auto hierarchy = inspect_deprecated::ReadFromVmo(tree.GetVmo()).take_value();
  auto* node = hierarchy.GetByPath({inspect_deprecated::kHealthNodeName});
  ASSERT_TRUE(node != nullptr);
  EXPECT_THAT(*node, NodeMatches(AllOf(
                         NameMatches(inspect_deprecated::kHealthNodeName),
                         PropertyList(UnorderedElementsAre(
                             StringPropertyIs("status", inspect_deprecated::kHealthStartingUp),
                             StringPropertyIs("message", "test"))))));
}

TEST(InspectHealth, CustomMessage) {
  auto tree = inspect_deprecated::Inspector().CreateTree("test");
  auto health = inspect_deprecated::NodeHealth(&tree.GetRoot());
  health.SetStatus("BAD CONFIG", "test");

  auto hierarchy = inspect_deprecated::ReadFromVmo(tree.GetVmo()).take_value();
  auto* node = hierarchy.GetByPath({inspect_deprecated::kHealthNodeName});
  ASSERT_TRUE(node != nullptr);
  EXPECT_THAT(*node, NodeMatches(AllOf(
                         NameMatches(inspect_deprecated::kHealthNodeName),
                         PropertyList(UnorderedElementsAre(StringPropertyIs("status", "BAD CONFIG"),
                                                           StringPropertyIs("message", "test"))))));
}

}  // namespace
