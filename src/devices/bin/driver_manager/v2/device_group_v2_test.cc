// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/v2/device_group_v2.h"

#include "src/devices/bin/driver_manager/v2/node.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

class FakeNodeManager : public dfv2::NodeManager {
 public:
  void Bind(dfv2::Node& node, std::shared_ptr<dfv2::BindResultTracker> result_tracker) override {}

  zx::result<dfv2::DriverHost*> CreateDriverHost() override { return zx::ok(nullptr); }
};

class DeviceGroupV2Test : public gtest::TestLoopFixture {};

TEST_F(DeviceGroupV2Test, DeviceGroupBind) {
  FakeNodeManager node_manager;
  fidl::Arena allocator;

  auto device_group = dfv2::DeviceGroupV2(
      DeviceGroupCreateInfo{
          .name = "group",
          .size = 2,
      },
      dispatcher(), &node_manager);

  auto matched_composite = fuchsia_driver_index::MatchedCompositeInfo(
      {.composite_name = "test-composite",
       .driver_info = fuchsia_driver_index::MatchedDriverInfo(
           {.url = "fuchsia-boot:///#meta/composite-driver.cm", .colocate = true})});

  // Bind the first node.
  auto node_1 =
      std::shared_ptr<dfv2::Node>(new dfv2::Node("group_node_1", {}, &node_manager, dispatcher()));
  auto matched_node_1 = fuchsia_driver_index::MatchedDeviceGroupInfo({
      .name = "group",
      .node_index = 0,
      .composite = matched_composite,
      .num_nodes = 2,
      .node_names = {{"node-0", "node-1"}},
      .primary_index = 1,
  });
  auto result = device_group.BindNode(fidl::ToWire(allocator, matched_node_1), node_1);
  ASSERT_TRUE(result.is_ok());
  ASSERT_FALSE(result.value());

  // Bind the second node.
  auto node_2 =
      std::shared_ptr<dfv2::Node>(new dfv2::Node("group_node_2", {}, &node_manager, dispatcher()));
  auto matched_node_2 = fuchsia_driver_index::MatchedDeviceGroupInfo({
      .name = "group",
      .node_index = 1,
      .composite = matched_composite,
      .num_nodes = 2,
      .node_names = {{"node-0", "node-1"}},
      .primary_index = 1,
  });
  result = device_group.BindNode(fidl::ToWire(allocator, matched_node_2), node_2);
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(result.value());

  // Verify the parents and primary node.
  auto composite_node_ptr = std::get<std::weak_ptr<dfv2::Node>>(result.value().value());
  auto composite_node = composite_node_ptr.lock();
  ASSERT_TRUE(composite_node);
  ASSERT_TRUE(composite_node->IsComposite());
  ASSERT_EQ("group_node_1", composite_node->parents()[0]->name());
  ASSERT_EQ("group_node_2", composite_node->parents()[1]->name());

  ASSERT_EQ("group_node_2", composite_node->GetPrimaryParent()->name());
}
