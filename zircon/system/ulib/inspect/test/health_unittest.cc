// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/cpp/health.h>
#include <lib/inspect/cpp/reader.h>
#include <zxtest/zxtest.h>

#include "lib/inspect/cpp/hierarchy.h"

namespace {

const inspect::StringPropertyValue* GetStringPropertyValue(const inspect::NodeValue& node,
                                                           const std::string& name) {
  for (const auto& property : node.properties()) {
    if (property.name() == name && property.Contains<inspect::StringPropertyValue>()) {
      return &property.Get<inspect::StringPropertyValue>();
    }
  }
  return nullptr;
}

bool ContainsProperty(const inspect::NodeValue& node, const std::string& name) {
  for (const auto& property : node.properties()) {
    if (property.name() == name) {
      return true;
    }
  }

  return false;
}

TEST(InspectHealth, Default) {
  auto inspector = inspect::Inspector("root");
  auto health = inspect::NodeHealth(&inspector.GetRoot());

  auto hierarchy = inspect::ReadFromVmo(*inspector.GetVmo().take_value()).take_value();
  auto* health_subtree = hierarchy.GetByPath({inspect::kHealthNodeName});
  ASSERT_TRUE(health_subtree != nullptr);
  EXPECT_STR_EQ(inspect::kHealthNodeName, health_subtree->name().c_str());

  auto* status = GetStringPropertyValue(health_subtree->node(), "status");
  ASSERT_TRUE(status != nullptr);
  ASSERT_FALSE(ContainsProperty(health_subtree->node(), "message"));
  EXPECT_STR_EQ(inspect::kHealthStartingUp, status->value().c_str());
}

TEST(InspectHealth, Ok) {
  auto inspector = inspect::Inspector("root");
  auto health = inspect::NodeHealth(&inspector.GetRoot());
  health.Ok();

  auto hierarchy = inspect::ReadFromVmo(*inspector.GetVmo().take_value()).take_value();
  auto* health_subtree = hierarchy.GetByPath({inspect::kHealthNodeName});
  ASSERT_TRUE(health_subtree != nullptr);
  EXPECT_STR_EQ(inspect::kHealthNodeName, health_subtree->name().c_str());

  auto* status = GetStringPropertyValue(health_subtree->node(), "status");
  ASSERT_TRUE(status != nullptr);
  ASSERT_FALSE(ContainsProperty(health_subtree->node(), "message"));
  EXPECT_STR_EQ(inspect::kHealthOk, status->value().c_str());
}

TEST(InspectHealth, UnhealthyToStartingUp) {
  auto inspector = inspect::Inspector("root");
  auto health = inspect::NodeHealth(&inspector.GetRoot());
  health.Unhealthy("test");
  health.StartingUp();

  auto hierarchy = inspect::ReadFromVmo(*inspector.GetVmo().take_value()).take_value();
  auto* health_subtree = hierarchy.GetByPath({inspect::kHealthNodeName});
  ASSERT_TRUE(health_subtree != nullptr);
  EXPECT_STR_EQ(inspect::kHealthNodeName, health_subtree->name().c_str());

  auto* status = GetStringPropertyValue(health_subtree->node(), "status");
  ASSERT_TRUE(status != nullptr);
  ASSERT_FALSE(ContainsProperty(health_subtree->node(), "message"));
  EXPECT_STR_EQ(inspect::kHealthStartingUp, status->value().c_str());
}

TEST(InspectHealth, Unhealthy) {
  auto inspector = inspect::Inspector("root");
  auto health = inspect::NodeHealth(&inspector.GetRoot());
  health.Unhealthy("test");

  auto hierarchy = inspect::ReadFromVmo(*inspector.GetVmo().take_value()).take_value();
  auto* health_subtree = hierarchy.GetByPath({inspect::kHealthNodeName});
  ASSERT_TRUE(health_subtree != nullptr);
  EXPECT_STR_EQ(inspect::kHealthNodeName, health_subtree->name().c_str());

  auto* status = GetStringPropertyValue(health_subtree->node(), "status");
  auto* message = GetStringPropertyValue(health_subtree->node(), "message");
  ASSERT_TRUE(status != nullptr);
  ASSERT_TRUE(message != nullptr);
  EXPECT_STR_EQ(inspect::kHealthUnhealthy, status->value().c_str());
  EXPECT_STR_EQ("test", message->value().c_str());
}

TEST(InspectHealth, StartingUpReason) {
  auto inspector = inspect::Inspector("root");
  auto health = inspect::NodeHealth(&inspector.GetRoot());
  health.StartingUp("test");

  auto hierarchy = inspect::ReadFromVmo(*inspector.GetVmo().take_value()).take_value();
  auto* health_subtree = hierarchy.GetByPath({inspect::kHealthNodeName});
  ASSERT_TRUE(health_subtree != nullptr);
  EXPECT_STR_EQ(inspect::kHealthNodeName, health_subtree->name().c_str());

  auto* status = GetStringPropertyValue(health_subtree->node(), "status");
  auto* message = GetStringPropertyValue(health_subtree->node(), "message");
  ASSERT_TRUE(status != nullptr);
  ASSERT_TRUE(message != nullptr);
  EXPECT_STR_EQ(inspect::kHealthStartingUp, status->value().c_str());
  EXPECT_STR_EQ("test", message->value().c_str());
}

TEST(InspectHealth, CustomMessage) {
  auto inspector = inspect::Inspector("root");
  auto health = inspect::NodeHealth(&inspector.GetRoot());
  health.SetStatus("BAD CONFIG", "test");

  auto hierarchy = inspect::ReadFromVmo(*inspector.GetVmo().take_value()).take_value();
  auto* health_subtree = hierarchy.GetByPath({inspect::kHealthNodeName});
  ASSERT_TRUE(health_subtree != nullptr);
  EXPECT_STR_EQ(inspect::kHealthNodeName, health_subtree->name().c_str());

  auto* status = GetStringPropertyValue(health_subtree->node(), "status");
  auto* message = GetStringPropertyValue(health_subtree->node(), "message");
  ASSERT_TRUE(status != nullptr);
  ASSERT_TRUE(message != nullptr);
  EXPECT_STR_EQ("BAD CONFIG", status->value().c_str());
  EXPECT_STR_EQ("test", message->value().c_str());
}

}  // namespace
