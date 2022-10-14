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

using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::StringIs;

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

}  // namespace
}  // namespace modular
