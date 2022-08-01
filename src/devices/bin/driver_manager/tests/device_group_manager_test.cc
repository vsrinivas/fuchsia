// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/device_group_manager.h"

#include <zxtest/zxtest.h>

#include "src/devices/bin/driver_manager/device_group.h"

namespace fdf = fuchsia_driver_framework;
namespace fdi = fuchsia_driver_index;

class FakeDeviceGroup : public DeviceGroup {
 public:
  explicit FakeDeviceGroup(fdf::wire::DeviceGroup group) : DeviceGroup(group) {}

  zx::status<> BindNodeToComposite(uint32_t node_index, DeviceOrNode node) override {
    return zx::ok();
  }
};

class FakeDeviceManagerBridge : public CompositeManagerBridge {
 public:
  zx::status<std::unique_ptr<DeviceGroup>> CreateDeviceGroup(
      fdf::wire::DeviceGroup group, fdi::wire::MatchedCompositeInfo driver) override {
    return zx::ok(std::make_unique<FakeDeviceGroup>(group));
  }

  // Match and bind all unbound nodes.
  void MatchAndBindAllNodes() override {}

  zx::status<fdi::wire::MatchedCompositeInfo> AddDeviceGroupToDriverIndex(
      fdf::wire::DeviceGroup group) override {
    auto iter = composite_matches.find(group.topological_path().get());
    if (iter == composite_matches.end()) {
      return zx::error(ZX_ERR_NOT_FOUND);
    }
    return zx::ok(iter->second);
  }

  void AddCompositeMatch(std::string_view topological_path,
                         fdi::wire::MatchedCompositeInfo composite) {
    composite_matches[topological_path] = composite;
  }

 private:
  std::unordered_map<std::string_view, fdi::wire::MatchedCompositeInfo> composite_matches;
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
      .name = fidl::StringView(allocator, "redstart"),
      .properties = node_props_1,
  };
  nodes[1] = fdf::wire::DeviceGroupNode{
      .name = fidl::StringView(allocator, "yellowthroat"),
      .properties = node_props_2,
  };

  auto device_group_path = std::string_view("/test/path");
  auto composite_match = fdi::wire::MatchedCompositeInfo::Builder(allocator)
                             .composite_name("ovenbird")
                             .node_index(2)
                             .num_nodes(3)
                             .Build();

  bridge_.AddCompositeMatch(device_group_path, composite_match);
  ASSERT_OK(device_group_manager_->AddDeviceGroup(
      fdf::wire::DeviceGroup::Builder(allocator)
          .topological_path(fidl::StringView(allocator, device_group_path))
          .nodes(std::move(nodes))
          .Build()));
  ASSERT_EQ(
      2, device_group_manager_->device_groups().at(device_group_path)->device_group_nodes().size());
  ASSERT_FALSE(device_group_manager_->device_groups()
                   .at(device_group_path)
                   ->device_group_nodes()[0]
                   .is_bound);
  ASSERT_FALSE(device_group_manager_->device_groups()
                   .at(device_group_path)
                   ->device_group_nodes()[1]
                   .is_bound);

  //  Bind device group node 2.
  auto matched_node_2 = MatchedDeviceGroupNodeInfo{};
  matched_node_2.groups.push_back(MatchedDeviceGroupInfo{
      .topological_path = device_group_path,
      .node_index = 1,
  });
  ASSERT_OK(device_group_manager_->BindDeviceGroupNode(matched_node_2,
                                                       fbl::RefPtr<DeviceNodeWrapper>(nullptr)));
  ASSERT_TRUE(device_group_manager_->device_groups()
                  .at(device_group_path)
                  ->device_group_nodes()[1]
                  .is_bound);

  //  Bind device group node 1.
  auto matched_node_1 = MatchedDeviceGroupNodeInfo{};
  matched_node_1.groups.push_back(MatchedDeviceGroupInfo{
      .topological_path = device_group_path,
      .node_index = 0,
  });
  ASSERT_OK(device_group_manager_->BindDeviceGroupNode(matched_node_1,
                                                       fbl::RefPtr<DeviceNodeWrapper>(nullptr)));
  ASSERT_TRUE(device_group_manager_->device_groups()
                  .at(device_group_path)
                  ->device_group_nodes()[0]
                  .is_bound);
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
      .name = fidl::StringView(allocator, "redstart"),
      .properties = node_props_1,
  };
  nodes[1] = fdf::wire::DeviceGroupNode{
      .name = fidl::StringView(allocator, "yellowthroat"),
      .properties = node_props_2,
  };

  auto device_group_path = std::string_view("/test/path");
  auto composite_match =
      fdi::wire::MatchedCompositeInfo::Builder(allocator).composite_name("ovenbird").Build();
  bridge_.AddCompositeMatch(device_group_path, composite_match);
  ASSERT_OK(device_group_manager_->AddDeviceGroup(
      fdf::wire::DeviceGroup::Builder(allocator)
          .topological_path(fidl::StringView(allocator, device_group_path))
          .nodes(std::move(nodes))
          .Build()));
  ASSERT_EQ(
      2, device_group_manager_->device_groups().at(device_group_path)->device_group_nodes().size());

  ASSERT_FALSE(device_group_manager_->device_groups()
                   .at(device_group_path)
                   ->device_group_nodes()[0]
                   .is_bound);
  ASSERT_FALSE(device_group_manager_->device_groups()
                   .at(device_group_path)
                   ->device_group_nodes()[1]
                   .is_bound);

  //  Bind device group node 1.
  auto matched_node = MatchedDeviceGroupNodeInfo{};
  matched_node.groups.push_back(MatchedDeviceGroupInfo{
      .topological_path = device_group_path,
      .node_index = 0,
  });
  ASSERT_OK(device_group_manager_->BindDeviceGroupNode(matched_node,
                                                       fbl::RefPtr<DeviceNodeWrapper>(nullptr)));
  ASSERT_TRUE(device_group_manager_->device_groups()
                  .at(device_group_path)
                  ->device_group_nodes()[0]
                  .is_bound);

  // Bind the same node.
  ASSERT_EQ(ZX_ERR_NOT_FOUND,
            device_group_manager_
                ->BindDeviceGroupNode(matched_node, fbl::RefPtr<DeviceNodeWrapper>(nullptr))
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
      .name = fidl::StringView(allocator, "phainopepla"),
      .properties = node_props_1,
  };
  nodes_1[1] = fdf::wire::DeviceGroupNode{
      .name = fidl::StringView(allocator, "pyrrhuloxia"),
      .properties = node_props_2,
  };

  auto device_group_path_1 = std::string_view("/test/path");
  bridge_.AddCompositeMatch(
      device_group_path_1,
      fdi::wire::MatchedCompositeInfo::Builder(allocator).composite_name("waxwing").Build());
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
      .name = fidl::StringView(allocator, "phainopepla"),
      .properties = node_props_2,
  };

  auto device_group_path_2 = std::string_view("/test/path2");
  bridge_.AddCompositeMatch(
      device_group_path_2,
      fdi::wire::MatchedCompositeInfo::Builder(allocator).composite_name("grosbeak").Build());
  ASSERT_OK(device_group_manager_->AddDeviceGroup(
      fdf::wire::DeviceGroup::Builder(allocator)
          .topological_path(fidl::StringView(allocator, device_group_path_2))
          .nodes(std::move(nodes_2))
          .Build()));
  ASSERT_EQ(
      1,
      device_group_manager_->device_groups().at(device_group_path_2)->device_group_nodes().size());

  // Bind the node that's in both device groups. The node should only bind to one
  // device group.
  auto matched_node = MatchedDeviceGroupNodeInfo{};
  matched_node.groups.push_back(MatchedDeviceGroupInfo{
      .topological_path = device_group_path_1,
      .node_index = 1,
  });
  matched_node.groups.push_back(MatchedDeviceGroupInfo{
      .topological_path = device_group_path_2,
      .node_index = 0,
  });
  ASSERT_OK(device_group_manager_->BindDeviceGroupNode(matched_node,
                                                       fbl::RefPtr<DeviceNodeWrapper>(nullptr)));
  ASSERT_TRUE(device_group_manager_->device_groups()
                  .at(device_group_path_1)
                  ->device_group_nodes()[1]
                  .is_bound);
  ASSERT_FALSE(device_group_manager_->device_groups()
                   .at(device_group_path_2)
                   ->device_group_nodes()[0]
                   .is_bound);

  // Bind the node again. Both device groups should now have the bound node.
  ASSERT_OK(device_group_manager_->BindDeviceGroupNode(matched_node,
                                                       fbl::RefPtr<DeviceNodeWrapper>(nullptr)));
  ASSERT_TRUE(device_group_manager_->device_groups()
                  .at(device_group_path_1)
                  ->device_group_nodes()[1]
                  .is_bound);
  ASSERT_TRUE(device_group_manager_->device_groups()
                  .at(device_group_path_2)
                  ->device_group_nodes()[0]
                  .is_bound);
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
      .name = fidl::StringView(allocator, "redstart"),
      .properties = node_props_1,
  };
  nodes[1] = fdf::wire::DeviceGroupNode{
      .name = fidl::StringView(allocator, "yellowthroat"),
      .properties = node_props_2,
  };

  auto device_group_path = std::string_view("/test/path");
  auto device_group = fdf::wire::DeviceGroup::Builder(allocator)
                          .topological_path(fidl::StringView(allocator, device_group_path))
                          .nodes(std::move(nodes))
                          .Build();
  ASSERT_OK(device_group_manager_->AddDeviceGroup(device_group));
  ASSERT_EQ(nullptr, device_group_manager_->device_groups().at(device_group_path));

  //  Bind device group node 1.
  auto matched_node = MatchedDeviceGroupNodeInfo{};
  matched_node.groups.push_back(MatchedDeviceGroupInfo{
      .topological_path = device_group_path,
      .node_index = 0,
  });
  ASSERT_EQ(ZX_ERR_NOT_FOUND,
            device_group_manager_
                ->BindDeviceGroupNode(matched_node, fbl::RefPtr<DeviceNodeWrapper>(nullptr))
                .status_value());

  // Bind a composite driver to the device group.
  auto composite_match =
      fdi::wire::MatchedCompositeInfo::Builder(allocator).composite_name("waxwing").Build();
  ASSERT_OK(device_group_manager_->BindAndCreateDeviceGroup(device_group, composite_match));
  ASSERT_EQ(
      1, device_group_manager_->device_groups().at(device_group_path)->device_group_nodes().size());

  // Reattempt binding the device group node 1. With a matched composite driver, it should
  // now bind successfully.
  ASSERT_OK(device_group_manager_->BindDeviceGroupNode(matched_node,
                                                       fbl::RefPtr<DeviceNodeWrapper>(nullptr)));
  ASSERT_TRUE(device_group_manager_->device_groups()
                  .at(device_group_path)
                  ->device_group_nodes()[0]
                  .is_bound);
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
      .name = fidl::StringView(allocator, "redstart"),
      .properties = node_props_1,
  };

  fidl::VectorView<fdf::wire::DeviceGroupNode> nodes_2(allocator, 1);
  nodes_2[0] = fdf::wire::DeviceGroupNode{
      .name = fidl::StringView(allocator, "yellowthroat"),
      .properties = node_props_1,
  };

  auto device_group = fdf::wire::DeviceGroup::Builder(allocator)
                          .topological_path(fidl::StringView(allocator, "/test/path"))
                          .nodes(std::move(nodes))
                          .Build();
  ASSERT_OK(device_group_manager_->AddDeviceGroup(device_group));

  auto device_group_2 = fdf::wire::DeviceGroup::Builder(allocator)
                            .topological_path(fidl::StringView(allocator, "/test/path"))
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
      .name = fidl::StringView(allocator, "redstart"),
      .properties = node_props_1,
  };
  nodes[1] = fdf::wire::DeviceGroupNode{
      .name = fidl::StringView(allocator, "yellowthroat"),
      .properties = node_props_2,
  };

  auto device_group_path = std::string_view("/test/path");
  auto composite_match = fdi::wire::MatchedCompositeInfo::Builder(allocator)
                             .composite_name("ovenbird")
                             .node_index(2)
                             .num_nodes(3)
                             .Build();
  bridge_.AddCompositeMatch(device_group_path, composite_match);

  auto device_group = fdf::wire::DeviceGroup::Builder(allocator)
                          .topological_path(fidl::StringView(allocator, device_group_path))
                          .nodes(std::move(nodes))
                          .Build();
  ASSERT_OK(device_group_manager_->AddDeviceGroup(device_group));
  ASSERT_EQ(
      2, device_group_manager_->device_groups().at(device_group_path)->device_group_nodes().size());

  ASSERT_EQ(ZX_ERR_ALREADY_BOUND,
            device_group_manager_->BindAndCreateDeviceGroup(device_group, composite_match)
                .status_value());
}
