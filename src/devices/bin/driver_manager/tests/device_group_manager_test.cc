// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/device_group/device_group_manager.h"

#include <zxtest/zxtest.h>

#include "src/devices/bin/driver_manager/device_group/device_group.h"
#include "src/devices/bin/driver_manager/v2/node.h"

namespace fdf = fuchsia_driver_framework;
namespace fdi = fuchsia_driver_index;

class FakeDeviceGroup : public DeviceGroup {
 public:
  explicit FakeDeviceGroup(size_t size) : DeviceGroup(size) {}

  zx::status<> BindNodeToComposite(uint32_t node_index, DeviceOrNode node) override {
    return zx::ok();
  }
};

class FakeDeviceManagerBridge : public CompositeManagerBridge {
 public:
  zx::status<std::unique_ptr<DeviceGroup>> CreateDeviceGroup(
      size_t size, fdi::MatchedCompositeInfo driver) override {
    return zx::ok(std::make_unique<FakeDeviceGroup>(size));
  }

  // Match and bind all unbound nodes.
  void BindNodesForDeviceGroups() override {}

  zx::status<fdi::MatchedCompositeInfo> AddDeviceGroupToDriverIndex(
      fdf::wire::DeviceGroup group) override {
    auto iter = composite_matches.find(std::string(group.topological_path().get()));
    if (iter == composite_matches.end()) {
      return zx::error(ZX_ERR_NOT_FOUND);
    }
    return zx::ok(iter->second);
  }

  void AddCompositeMatch(std::string_view topological_path,
                         const fdi::MatchedCompositeInfo composite) {
    composite_matches[std::string(topological_path)] = composite;
  }

 private:
  std::unordered_map<std::string, fdi::MatchedCompositeInfo> composite_matches;
};

class DeviceGroupManagerTest : public zxtest::Test {
 public:
  void SetUp() override { device_group_manager_ = std::make_unique<DeviceGroupManager>(&bridge_); }

  std::unique_ptr<DeviceGroupManager> device_group_manager_;
  FakeDeviceManagerBridge bridge_;
};

TEST_F(DeviceGroupManagerTest, TestAddMatchDeviceGroup) {
  fidl::Arena allocator;

  fidl::VectorView<fdf::wire::DeviceGroupProperty> node_props_1(allocator, 1);
  auto prop_vals_1 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 1);
  prop_vals_1[0] = fdf::wire::NodePropertyValue::WithIntValue(10);
  node_props_1[0] = fdf::wire::DeviceGroupProperty{
      .key = fdf::wire::NodePropertyKey::WithIntValue(1),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_1,
  };

  fidl::VectorView<fdf::wire::DeviceGroupProperty> node_props_2(allocator, 2);
  auto prop_vals_2 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 2);
  prop_vals_2[0] = fdf::wire::NodePropertyValue::WithIntValue(10);
  node_props_2[0] = fdf::wire::DeviceGroupProperty{
      .key = fdf::wire::NodePropertyKey::WithIntValue(1),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_2,
  };

  fidl::VectorView<fdf::wire::DeviceGroupNode> nodes(allocator, 2);
  nodes[0] = fdf::wire::DeviceGroupNode{
      .properties = node_props_1,
  };
  nodes[1] = fdf::wire::DeviceGroupNode{
      .properties = node_props_2,
  };

  auto device_group_path = "/test/path";
  fdi::MatchedCompositeInfo composite_match{{
      .composite_name = "ovenbird",
      .node_index = 2,
      .num_nodes = 3,
  }};
  bridge_.AddCompositeMatch(device_group_path, composite_match);
  ASSERT_OK(device_group_manager_->AddDeviceGroup(
      fdf::wire::DeviceGroup::Builder(allocator)
          .topological_path(fidl::StringView(allocator, device_group_path))
          .nodes(std::move(nodes))
          .Build()));
  ASSERT_EQ(
      2, device_group_manager_->device_groups().at(device_group_path)->device_group_nodes().size());
  ASSERT_FALSE(
      device_group_manager_->device_groups().at(device_group_path)->device_group_nodes()[0]);
  ASSERT_FALSE(
      device_group_manager_->device_groups().at(device_group_path)->device_group_nodes()[1]);

  //  Bind device group node 2.
  auto matched_node_2 = fdi::MatchedDeviceGroupNodeInfo{{
      .device_groups = std::vector<fdi::MatchedDeviceGroupInfo>(),
  }};
  matched_node_2.device_groups()->push_back(fdi::MatchedDeviceGroupInfo{{
      .topological_path = device_group_path,
      .node_index = 1,
  }});
  ASSERT_OK(device_group_manager_->BindDeviceGroupNode(matched_node_2, std::weak_ptr<Node>()));
  ASSERT_TRUE(
      device_group_manager_->device_groups().at(device_group_path)->device_group_nodes()[1]);

  //  Bind device group node 1.
  auto matched_node_1 = fdi::MatchedDeviceGroupNodeInfo{{
      .device_groups = std::vector<fdi::MatchedDeviceGroupInfo>(),
  }};
  matched_node_1.device_groups()->push_back(fdi::MatchedDeviceGroupInfo{{
      .topological_path = device_group_path,
      .node_index = 0,
  }});
  ASSERT_OK(device_group_manager_->BindDeviceGroupNode(matched_node_1, std::weak_ptr<Node>()));
  ASSERT_TRUE(
      device_group_manager_->device_groups().at(device_group_path)->device_group_nodes()[0]);
}

TEST_F(DeviceGroupManagerTest, TestBindSameNodeTwice) {
  fidl::Arena allocator;

  fidl::VectorView<fdf::wire::DeviceGroupProperty> node_props_1(allocator, 1);
  auto prop_vals_1 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 1);
  prop_vals_1[0] = fdf::wire::NodePropertyValue::WithIntValue(10);
  node_props_1[0] = fdf::wire::DeviceGroupProperty{
      .key = fdf::wire::NodePropertyKey::WithIntValue(1),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_1,
  };

  fidl::VectorView<fdf::wire::DeviceGroupProperty> node_props_2(allocator, 2);
  auto prop_vals_2 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 2);
  prop_vals_2[0] = fdf::wire::NodePropertyValue::WithIntValue(10);
  node_props_2[0] = fdf::wire::DeviceGroupProperty{
      .key = fdf::wire::NodePropertyKey::WithIntValue(1),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_2,
  };

  fidl::VectorView<fdf::wire::DeviceGroupNode> nodes(allocator, 2);
  nodes[0] = fdf::wire::DeviceGroupNode{
      .properties = node_props_1,
  };
  nodes[1] = fdf::wire::DeviceGroupNode{
      .properties = node_props_2,
  };

  auto device_group_path = "/test/path";
  fdi::MatchedCompositeInfo composite_match{{
      .composite_name = "ovenbird",
  }};
  bridge_.AddCompositeMatch(device_group_path, composite_match);
  ASSERT_OK(device_group_manager_->AddDeviceGroup(
      fdf::wire::DeviceGroup::Builder(allocator)
          .topological_path(fidl::StringView(allocator, device_group_path))
          .nodes(std::move(nodes))
          .Build()));
  ASSERT_EQ(
      2, device_group_manager_->device_groups().at(device_group_path)->device_group_nodes().size());

  ASSERT_FALSE(
      device_group_manager_->device_groups().at(device_group_path)->device_group_nodes()[0]);
  ASSERT_FALSE(
      device_group_manager_->device_groups().at(device_group_path)->device_group_nodes()[1]);

  //  Bind device group node 1.
  auto matched_node = fdi::MatchedDeviceGroupNodeInfo{{
      .device_groups = std::vector<fdi::MatchedDeviceGroupInfo>(),
  }};
  matched_node.device_groups()->push_back(fdi::MatchedDeviceGroupInfo{{
      .topological_path = device_group_path,
      .node_index = 0,
  }});
  ASSERT_OK(device_group_manager_->BindDeviceGroupNode(matched_node, std::weak_ptr<Node>()));
  ASSERT_TRUE(
      device_group_manager_->device_groups().at(device_group_path)->device_group_nodes()[0]);

  // Bind the same node.
  ASSERT_EQ(ZX_ERR_NOT_FOUND,
            device_group_manager_->BindDeviceGroupNode(matched_node, std::weak_ptr<Node>())
                .status_value());
}

TEST_F(DeviceGroupManagerTest, TestMultibind) {
  fidl::Arena allocator;

  // Add the first device group.
  fidl::VectorView<fdf::wire::DeviceGroupProperty> node_props_1(allocator, 1);
  auto prop_vals_1 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 1);
  prop_vals_1[0] = fdf::wire::NodePropertyValue::WithIntValue(10);
  node_props_1[0] = fdf::wire::DeviceGroupProperty{
      .key = fdf::wire::NodePropertyKey::WithIntValue(1),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_1,
  };

  fidl::VectorView<fdf::wire::DeviceGroupProperty> node_props_2(allocator, 2);
  auto prop_vals_2 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 2);
  prop_vals_2[0] = fdf::wire::NodePropertyValue::WithIntValue(10);
  node_props_2[0] = fdf::wire::DeviceGroupProperty{
      .key = fdf::wire::NodePropertyKey::WithIntValue(1),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_2,
  };

  fidl::VectorView<fdf::wire::DeviceGroupNode> nodes_1(allocator, 2);
  nodes_1[0] = fdf::wire::DeviceGroupNode{
      .properties = node_props_1,
  };
  nodes_1[1] = fdf::wire::DeviceGroupNode{
      .properties = node_props_2,
  };

  auto device_group_path_1 = "/test/path";
  bridge_.AddCompositeMatch(device_group_path_1, fdi::MatchedCompositeInfo{{
                                                     .composite_name = "waxwing",
                                                 }});
  ASSERT_OK(device_group_manager_->AddDeviceGroup(
      fdf::wire::DeviceGroup::Builder(allocator)
          .topological_path(fidl::StringView(allocator, device_group_path_1))
          .nodes(std::move(nodes_1))
          .Build()));
  ASSERT_EQ(
      2,
      device_group_manager_->device_groups().at(device_group_path_1)->device_group_nodes().size());

  // Add a second device group with a node that's the same as one in the first device group.
  fidl::VectorView<fdf::wire::DeviceGroupNode> nodes_2(allocator, 1);
  nodes_2[0] = fdf::wire::DeviceGroupNode{
      .properties = node_props_2,
  };

  auto device_group_path_2 = "/test/path2";
  bridge_.AddCompositeMatch(device_group_path_2, fdi::MatchedCompositeInfo{{
                                                     .composite_name = "grosbeak",
                                                 }});
  ASSERT_OK(device_group_manager_->AddDeviceGroup(
      fdf::wire::DeviceGroup::Builder(allocator)
          .topological_path(fidl::StringView(allocator, device_group_path_2))
          .nodes(std::move(nodes_2))
          .Build()));
  ASSERT_EQ(
      1,
      device_group_manager_->device_groups().at(device_group_path_2)->device_group_nodes().size());

  // Bind the node that's in both device device_groups(). The node should only bind to one
  // device group.
  auto matched_node = fdi::MatchedDeviceGroupNodeInfo{{
      .device_groups = std::vector<fdi::MatchedDeviceGroupInfo>(),
  }};
  matched_node.device_groups()->push_back(fdi::MatchedDeviceGroupInfo{{
      .topological_path = device_group_path_1,
      .node_index = 1,
  }});
  matched_node.device_groups()->push_back(fdi::MatchedDeviceGroupInfo{{
      .topological_path = device_group_path_2,
      .node_index = 0,
  }});
  ASSERT_OK(device_group_manager_->BindDeviceGroupNode(matched_node, std::weak_ptr<Node>()));
  ASSERT_TRUE(
      device_group_manager_->device_groups().at(device_group_path_1)->device_group_nodes()[1]);
  ASSERT_FALSE(
      device_group_manager_->device_groups().at(device_group_path_2)->device_group_nodes()[0]);

  // Bind the node again. Both device groups should now have the bound node.
  ASSERT_OK(device_group_manager_->BindDeviceGroupNode(matched_node, std::weak_ptr<Node>()));
  ASSERT_TRUE(
      device_group_manager_->device_groups().at(device_group_path_1)->device_group_nodes()[1]);
  ASSERT_TRUE(
      device_group_manager_->device_groups().at(device_group_path_2)->device_group_nodes()[0]);
}

TEST_F(DeviceGroupManagerTest, TestBindWithNoCompositeMatch) {
  fidl::Arena allocator;

  fidl::VectorView<fdf::wire::DeviceGroupProperty> node_props_1(allocator, 1);
  auto prop_vals_1 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 1);
  prop_vals_1[0] = fdf::wire::NodePropertyValue::WithIntValue(10);
  node_props_1[0] = fdf::wire::DeviceGroupProperty{
      .key = fdf::wire::NodePropertyKey::WithIntValue(1),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_1,
  };

  fidl::VectorView<fdf::wire::DeviceGroupProperty> node_props_2(allocator, 2);
  auto prop_vals_2 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 2);
  prop_vals_2[0] = fdf::wire::NodePropertyValue::WithIntValue(10);
  node_props_2[0] = fdf::wire::DeviceGroupProperty{
      .key = fdf::wire::NodePropertyKey::WithIntValue(1),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_2,
  };

  fidl::VectorView<fdf::wire::DeviceGroupNode> nodes(allocator, 2);
  nodes[0] = fdf::wire::DeviceGroupNode{
      .properties = node_props_1,
  };
  nodes[1] = fdf::wire::DeviceGroupNode{
      .properties = node_props_2,
  };

  auto device_group_path = "/test/path";
  auto device_group = fdf::wire::DeviceGroup::Builder(allocator)
                          .topological_path(fidl::StringView(allocator, device_group_path))
                          .nodes(std::move(nodes))
                          .Build();
  ASSERT_OK(device_group_manager_->AddDeviceGroup(device_group));
  ASSERT_EQ(nullptr, device_group_manager_->device_groups().at(device_group_path));

  //  Bind device group node 1.
  auto matched_node = fdi::MatchedDeviceGroupNodeInfo{{
      .device_groups = std::vector<fdi::MatchedDeviceGroupInfo>(),
  }};
  matched_node.device_groups()->push_back(fdi::MatchedDeviceGroupInfo{{
      .topological_path = device_group_path,
      .node_index = 0,
  }});
  ASSERT_EQ(ZX_ERR_NOT_FOUND,
            device_group_manager_->BindDeviceGroupNode(matched_node, std::weak_ptr<Node>())
                .status_value());

  // Bind a composite driver to the device group.
  fdi::MatchedCompositeInfo composite_match{{
      .composite_name = "waxwing",
  }};
  ASSERT_OK(device_group_manager_->BindAndCreateDeviceGroup(
      device_group.nodes().count(), device_group.topological_path().get(), composite_match));
  ASSERT_EQ(
      2, device_group_manager_->device_groups().at(device_group_path)->device_group_nodes().size());

  // Reattempt binding the device group node 1. With a matched composite driver, it should
  // now bind successfully.
  ASSERT_OK(device_group_manager_->BindDeviceGroupNode(matched_node, std::weak_ptr<Node>()));
  ASSERT_TRUE(
      device_group_manager_->device_groups().at(device_group_path)->device_group_nodes()[0]);
}

TEST_F(DeviceGroupManagerTest, TestAddDuplicate) {
  fidl::Arena allocator;

  fidl::VectorView<fdf::wire::DeviceGroupProperty> node_props_1(allocator, 1);
  auto prop_vals_1 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 1);
  prop_vals_1[0] = fdf::wire::NodePropertyValue::WithIntValue(10);
  node_props_1[0] = fdf::wire::DeviceGroupProperty{
      .key = fdf::wire::NodePropertyKey::WithIntValue(1),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_1,
  };

  fidl::VectorView<fdf::wire::DeviceGroupNode> nodes(allocator, 1);
  nodes[0] = fdf::wire::DeviceGroupNode{
      .properties = node_props_1,
  };

  fidl::VectorView<fdf::wire::DeviceGroupNode> nodes_2(allocator, 1);
  nodes_2[0] = fdf::wire::DeviceGroupNode{
      .properties = node_props_1,
  };

  auto device_group_path = "/test/path";
  bridge_.AddCompositeMatch(device_group_path, fdi::MatchedCompositeInfo{{
                                                   .composite_name = "grosbeak",
                                               }});

  auto device_group = fdf::wire::DeviceGroup::Builder(allocator)
                          .topological_path(fidl::StringView(allocator, device_group_path))
                          .nodes(std::move(nodes))
                          .Build();
  ASSERT_OK(device_group_manager_->AddDeviceGroup(device_group));

  auto device_group_2 = fdf::wire::DeviceGroup::Builder(allocator)
                            .topological_path(fidl::StringView(allocator, device_group_path))
                            .nodes(std::move(nodes_2))
                            .Build();
  ASSERT_EQ(ZX_ERR_INVALID_ARGS,
            device_group_manager_->AddDeviceGroup(device_group_2).status_value());
}

TEST_F(DeviceGroupManagerTest, TestRebindCompositeMatch) {
  fidl::Arena allocator;

  fidl::VectorView<fdf::wire::DeviceGroupProperty> node_props_1(allocator, 1);
  auto prop_vals_1 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 1);
  prop_vals_1[0] = fdf::wire::NodePropertyValue::WithIntValue(10);
  node_props_1[0] = fdf::wire::DeviceGroupProperty{
      .key = fdf::wire::NodePropertyKey::WithIntValue(1),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_1,
  };

  fidl::VectorView<fdf::wire::DeviceGroupProperty> node_props_2(allocator, 2);
  auto prop_vals_2 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 2);
  prop_vals_2[0] = fdf::wire::NodePropertyValue::WithIntValue(10);
  node_props_2[0] = fdf::wire::DeviceGroupProperty{
      .key = fdf::wire::NodePropertyKey::WithIntValue(1),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_2,
  };

  fidl::VectorView<fdf::wire::DeviceGroupNode> nodes(allocator, 2);
  nodes[0] = fdf::wire::DeviceGroupNode{
      .properties = node_props_1,
  };
  nodes[1] = fdf::wire::DeviceGroupNode{
      .properties = node_props_2,
  };

  auto device_group_path = "/test/path";
  fdi::MatchedCompositeInfo composite_match{{
      .composite_name = "ovenbird",
      .node_index = 2,
      .num_nodes = 3,
  }};
  bridge_.AddCompositeMatch(device_group_path, composite_match);

  auto device_group = fdf::wire::DeviceGroup::Builder(allocator)
                          .topological_path(fidl::StringView(allocator, device_group_path))
                          .nodes(std::move(nodes))
                          .Build();
  ASSERT_OK(device_group_manager_->AddDeviceGroup(device_group));
  ASSERT_EQ(
      2, device_group_manager_->device_groups().at(device_group_path)->device_group_nodes().size());

  ASSERT_EQ(ZX_ERR_ALREADY_BOUND,
            device_group_manager_
                ->BindAndCreateDeviceGroup(device_group.nodes().count(),
                                           device_group.topological_path().get(), composite_match)
                .status_value());
}
