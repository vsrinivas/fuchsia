// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/cpp/health.h>
#include <lib/inspect/cpp/reader.h>

#include <zxtest/zxtest.h>

#include "lib/inspect/cpp/hierarchy.h"

namespace {

// Example:
// auto* value = GetPropertyValue<inspect::StringPropertyValue>(node, "name");
template <class T>
const T* GetPropertyValue(const inspect::NodeValue& node, const std::string& name) {
  for (const auto& property : node.properties()) {
    if (property.name() == name && property.Contains<T>()) {
      return &property.Get<T>();
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
  auto inspector = inspect::Inspector();
  auto health = inspect::NodeHealth(&inspector.GetRoot(), []() -> zx_time_t { return 42; });

  auto hierarchy = inspect::ReadFromVmo(inspector.DuplicateVmo()).take_value();
  const auto* health_subtree = hierarchy.GetByPath({inspect::kHealthNodeName});
  ASSERT_TRUE(health_subtree != nullptr);
  EXPECT_STR_EQ(inspect::kHealthNodeName, health_subtree->name().c_str());

  const auto* status =
      GetPropertyValue<inspect::StringPropertyValue>(health_subtree->node(), "status");
  ASSERT_TRUE(status != nullptr);
  ASSERT_FALSE(ContainsProperty(health_subtree->node(), "message"));
  EXPECT_STR_EQ(inspect::kHealthStartingUp, status->value().c_str());

  const auto* start_time =
      GetPropertyValue<inspect::IntPropertyValue>(health_subtree->node(), "start_timestamp_nanos");
  ASSERT_TRUE(status != nullptr);
  EXPECT_EQ(42L, start_time->value());
}

TEST(InspectHealth, Ok) {
  auto inspector = inspect::Inspector();
  auto health = inspect::NodeHealth(&inspector.GetRoot());
  health.Ok();

  const auto hierarchy = inspect::ReadFromVmo(inspector.DuplicateVmo()).take_value();
  const auto* health_subtree = hierarchy.GetByPath({inspect::kHealthNodeName});
  ASSERT_TRUE(health_subtree != nullptr);
  EXPECT_STR_EQ(inspect::kHealthNodeName, health_subtree->name().c_str());

  const auto* status =
      GetPropertyValue<inspect::StringPropertyValue>(health_subtree->node(), "status");
  ASSERT_TRUE(status != nullptr);
  ASSERT_FALSE(ContainsProperty(health_subtree->node(), "message"));
  EXPECT_STR_EQ(inspect::kHealthOk, status->value().c_str());
}

TEST(InspectHealth, UnhealthyToStartingUp) {
  auto inspector = inspect::Inspector();
  auto health = inspect::NodeHealth(&inspector.GetRoot());
  health.Unhealthy("test");
  health.StartingUp();

  const auto hierarchy = inspect::ReadFromVmo(inspector.DuplicateVmo()).take_value();
  const auto* health_subtree = hierarchy.GetByPath({inspect::kHealthNodeName});
  ASSERT_TRUE(health_subtree != nullptr);
  EXPECT_STR_EQ(inspect::kHealthNodeName, health_subtree->name().c_str());

  const auto* status =
      GetPropertyValue<inspect::StringPropertyValue>(health_subtree->node(), "status");
  ASSERT_TRUE(status != nullptr);
  ASSERT_FALSE(ContainsProperty(health_subtree->node(), "message"));
  EXPECT_STR_EQ(inspect::kHealthStartingUp, status->value().c_str());
}

TEST(InspectHealth, Unhealthy) {
  auto inspector = inspect::Inspector();
  auto health = inspect::NodeHealth(&inspector.GetRoot());
  health.Unhealthy("test");

  auto hierarchy = inspect::ReadFromVmo(inspector.DuplicateVmo()).take_value();
  const auto* health_subtree = hierarchy.GetByPath({inspect::kHealthNodeName});
  ASSERT_TRUE(health_subtree != nullptr);
  EXPECT_STR_EQ(inspect::kHealthNodeName, health_subtree->name().c_str());

  const auto* status =
      GetPropertyValue<inspect::StringPropertyValue>(health_subtree->node(), "status");
  const auto* message =
      GetPropertyValue<inspect::StringPropertyValue>(health_subtree->node(), "message");
  ASSERT_TRUE(status != nullptr);
  ASSERT_TRUE(message != nullptr);
  EXPECT_STR_EQ(inspect::kHealthUnhealthy, status->value().c_str());
  EXPECT_STR_EQ("test", message->value().c_str());
}

TEST(InspectHealth, StartingUpReason) {
  auto inspector = inspect::Inspector();
  auto health = inspect::NodeHealth(&inspector.GetRoot());
  health.StartingUp("test");

  auto hierarchy = inspect::ReadFromVmo(inspector.DuplicateVmo()).take_value();
  const auto* health_subtree = hierarchy.GetByPath({inspect::kHealthNodeName});
  ASSERT_TRUE(health_subtree != nullptr);
  EXPECT_STR_EQ(inspect::kHealthNodeName, health_subtree->name().c_str());

  const auto* status =
      GetPropertyValue<inspect::StringPropertyValue>(health_subtree->node(), "status");
  const auto* message =
      GetPropertyValue<inspect::StringPropertyValue>(health_subtree->node(), "message");
  ASSERT_TRUE(status != nullptr);
  ASSERT_TRUE(message != nullptr);
  EXPECT_STR_EQ(inspect::kHealthStartingUp, status->value().c_str());
  EXPECT_STR_EQ("test", message->value().c_str());
}

TEST(InspectHealth, CustomMessage) {
  auto inspector = inspect::Inspector();
  auto health = inspect::NodeHealth(&inspector.GetRoot());
  health.SetStatus("BAD CONFIG", "test");

  auto hierarchy = inspect::ReadFromVmo(inspector.DuplicateVmo()).take_value();
  const auto* health_subtree = hierarchy.GetByPath({inspect::kHealthNodeName});
  ASSERT_TRUE(health_subtree != nullptr);
  EXPECT_STR_EQ(inspect::kHealthNodeName, health_subtree->name().c_str());

  const auto* status =
      GetPropertyValue<inspect::StringPropertyValue>(health_subtree->node(), "status");
  const auto* message =
      GetPropertyValue<inspect::StringPropertyValue>(health_subtree->node(), "message");
  ASSERT_TRUE(status != nullptr);
  ASSERT_TRUE(message != nullptr);
  EXPECT_STR_EQ("BAD CONFIG", status->value().c_str());
  EXPECT_STR_EQ("test", message->value().c_str());
}

}  // namespace
