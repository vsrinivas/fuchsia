// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inspector.h"

#include <lib/inspect/testing/cpp/inspect.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <rapidjson/document.h>

namespace modular {

namespace {

using inspect::testing::ChildrenMatch;
using inspect::testing::IntIs;
using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::StringIs;
using ::testing::IsSupersetOf;
using ::testing::SizeIs;

TEST(InspectorTest, AddConfig) {
  inspect::Inspector inspector;
  modular::BasemgrInspector basemgr_inspector(&inspector);

  // Create a config and add it to the inspector.
  auto config = modular::DefaultConfig();
  auto config_json = modular::ConfigToJsonString(config);

  basemgr_inspector.AddConfig(config);

  // Read the inspect hierarchy.
  auto hierarchy = inspect::ReadFromVmo(inspector.DuplicateVmo());
  ASSERT_TRUE(hierarchy.is_ok());

  EXPECT_THAT(
      hierarchy.take_value(),
      NodeMatches(AllOf(NameMatches("root"),
                        PropertyList(UnorderedElementsAre(StringIs("config", config_json))))));
}

TEST(InspectorTest, AddSessionStartedAt) {
  inspect::Inspector inspector;
  modular::BasemgrInspector basemgr_inspector(&inspector);

  zx_time_t expected_time = 1234;
  basemgr_inspector.AddSessionStartedAt(expected_time);

  // Read the inspect hierarchy.
  auto hierarchy = inspect::ReadFromVmo(inspector.DuplicateVmo());
  ASSERT_TRUE(hierarchy.is_ok());

  EXPECT_THAT(
      hierarchy.take_value(),
      ChildrenMatch(ElementsAre(AllOf(
          NodeMatches(NameMatches(kInspectSessionStartedAtNodeName)),
          ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
              NameMatches("0"),
              PropertyList(ElementsAre(IntIs(kInspectTimePropertyName, expected_time)))))))))));
}

TEST(InspectorTest, AddSessionStartedAtCapacity) {
  inspect::Inspector inspector;
  modular::BasemgrInspector basemgr_inspector(&inspector);

  // Add enough timestamps to fill the list capacity, plus one.
  // This ensures that the first timestamp added will be evicted.
  for (size_t i = 0; i <= kInspectSessionStartedAtCapacity; ++i) {
    basemgr_inspector.AddSessionStartedAt(static_cast<zx_time_t>(i));
  }

  // Read the inspect hierarchy.
  auto hierarchy = inspect::ReadFromVmo(inspector.DuplicateVmo());
  ASSERT_TRUE(hierarchy.is_ok());

  EXPECT_THAT(hierarchy.take_value(),
              ChildrenMatch(ElementsAre(AllOf(
                  NodeMatches(NameMatches(kInspectSessionStartedAtNodeName)),
                  ChildrenMatch(AllOf(
                      // The list should not contain more than the allowed capacity of items.
                      SizeIs(kInspectSessionStartedAtCapacity),
                      // The last timestamp inserted should be kept.
                      IsSupersetOf({NodeMatches(PropertyList(ElementsAre(IntIs(
                          kInspectTimePropertyName, kInspectSessionStartedAtCapacity))))})))))));
}

}  // namespace
}  // namespace modular
