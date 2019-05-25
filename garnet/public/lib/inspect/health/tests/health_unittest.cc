// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/health/health.h>
#include <lib/inspect/testing/inspect.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/inspect/reader.h"

using namespace inspect::testing;
using testing::ElementsAre;
using testing::IsEmpty;
using testing::UnorderedElementsAre;

namespace {

TEST(InspectHealth, Default) {
  auto tree = inspect::Inspector().CreateTree("test");
  auto health = inspect::NodeHealth(&tree.GetRoot());

  auto hierarchy = inspect::ReadFromVmo(tree.GetVmo()).take_value();
  auto* node = hierarchy.GetByPath({inspect::kHealthNodeName});
  ASSERT_TRUE(node != nullptr);
  EXPECT_THAT(*node, NodeMatches(AllOf(
                         NameMatches(inspect::kHealthNodeName),
                         PropertyList(UnorderedElementsAre(StringPropertyIs(
                             "status", inspect::kHealthStartingUp))))));
}

TEST(InspectHealth, Ok) {
  auto tree = inspect::Inspector().CreateTree("test");
  auto health = inspect::NodeHealth(&tree.GetRoot());
  health.Ok();

  auto hierarchy = inspect::ReadFromVmo(tree.GetVmo()).take_value();
  auto* node = hierarchy.GetByPath({inspect::kHealthNodeName});
  ASSERT_TRUE(node != nullptr);
  EXPECT_THAT(*node, NodeMatches(AllOf(
                         NameMatches(inspect::kHealthNodeName),
                         PropertyList(UnorderedElementsAre(StringPropertyIs(
                             "status", inspect::kHealthOk))))));
}

TEST(InspectHealth, UnhealthyToStartingUp) {
  auto tree = inspect::Inspector().CreateTree("test");
  auto health = inspect::NodeHealth(&tree.GetRoot());
  health.Unhealthy("test");
  health.StartingUp();

  auto hierarchy = inspect::ReadFromVmo(tree.GetVmo()).take_value();
  auto* node = hierarchy.GetByPath({inspect::kHealthNodeName});
  ASSERT_TRUE(node != nullptr);
  EXPECT_THAT(*node, NodeMatches(AllOf(
                         NameMatches(inspect::kHealthNodeName),
                         PropertyList(UnorderedElementsAre(StringPropertyIs(
                             "status", inspect::kHealthStartingUp))))));
}

TEST(InspectHealth, Unhealthy) {
  auto tree = inspect::Inspector().CreateTree("test");
  auto health = inspect::NodeHealth(&tree.GetRoot());
  health.Unhealthy("test");

  auto hierarchy = inspect::ReadFromVmo(tree.GetVmo()).take_value();
  auto* node = hierarchy.GetByPath({inspect::kHealthNodeName});
  ASSERT_TRUE(node != nullptr);
  EXPECT_THAT(*node,
              NodeMatches(AllOf(
                  NameMatches(inspect::kHealthNodeName),
                  PropertyList(UnorderedElementsAre(
                      StringPropertyIs("status", inspect::kHealthUnhealthy),
                      StringPropertyIs("message", "test"))))));
}

TEST(InspectHealth, StartingUpReason) {
  auto tree = inspect::Inspector().CreateTree("test");
  auto health = inspect::NodeHealth(&tree.GetRoot());
  health.StartingUp("test");

  auto hierarchy = inspect::ReadFromVmo(tree.GetVmo()).take_value();
  auto* node = hierarchy.GetByPath({inspect::kHealthNodeName});
  ASSERT_TRUE(node != nullptr);
  EXPECT_THAT(*node,
              NodeMatches(AllOf(
                  NameMatches(inspect::kHealthNodeName),
                  PropertyList(UnorderedElementsAre(
                      StringPropertyIs("status", inspect::kHealthStartingUp),
                      StringPropertyIs("message", "test"))))));
}

TEST(InspectHealth, CustomMessage) {
  auto tree = inspect::Inspector().CreateTree("test");
  auto health = inspect::NodeHealth(&tree.GetRoot());
  health.SetStatus("BAD CONFIG", "test");

  auto hierarchy = inspect::ReadFromVmo(tree.GetVmo()).take_value();
  auto* node = hierarchy.GetByPath({inspect::kHealthNodeName});
  ASSERT_TRUE(node != nullptr);
  EXPECT_THAT(*node,
              NodeMatches(AllOf(NameMatches(inspect::kHealthNodeName),
                                PropertyList(UnorderedElementsAre(
                                    StringPropertyIs("status", "BAD CONFIG"),
                                    StringPropertyIs("message", "test"))))));
}

}  // namespace
