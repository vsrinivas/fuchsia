// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/device_group/device_group_manager.h"

#include <lib/fit/defer.h>

#include <zxtest/zxtest.h>

#include "src/devices/bin/driver_manager/device_group/device_group.h"
#include "src/devices/bin/driver_manager/v2/node.h"

namespace fdf = fuchsia_driver_framework;
namespace fdi = fuchsia_driver_index;

class FakeDeviceGroup : public DeviceGroup {
 public:
  explicit FakeDeviceGroup(DeviceGroupCreateInfo create_info)
      : DeviceGroup(std::move(create_info)) {}

  zx::result<std::optional<DeviceOrNode>> BindNodeImpl(
      fuchsia_driver_index::wire::MatchedDeviceGroupInfo info,
      const DeviceOrNode& device_or_node) override {
    return zx::ok(std::nullopt);
  }
};

class FakeDeviceManagerBridge : public CompositeManagerBridge {
 public:
  // CompositeManagerBridge:
  void BindNodesForDeviceGroups() override {}
  void AddDeviceGroupToDriverIndex(fdf::wire::DeviceGroup group,
                                   AddToIndexCallback callback) override {
    auto iter = device_group_matches_.find(std::string(group.topological_path().get()));
    zx::result<fdi::DriverIndexAddDeviceGroupResponse> result;
    if (iter == device_group_matches_.end()) {
      result = zx::error(ZX_ERR_NOT_FOUND);
    } else {
      auto composite = iter->second.composite();
      auto names = iter->second.node_names();
      ZX_ASSERT(composite.has_value());
      ZX_ASSERT(names.has_value());
      result = zx::ok(fdi::DriverIndexAddDeviceGroupResponse(composite.value(), names.value()));
    }
    auto defer =
        fit::defer([callback = std::move(callback), result]() mutable { callback(result); });
  }

  void AddDeviceGroupMatch(std::string_view topological_path, fdi::MatchedDeviceGroupInfo match) {
    device_group_matches_[std::string(topological_path)] = std::move(match);
  }

 private:
  // Stores matches for each device group topological path, that get returned to the
  // AddToIndexCallback that is given in AddDeviceGroupToDriverIndex.
  std::unordered_map<std::string, fdi::MatchedDeviceGroupInfo> device_group_matches_;
};

class DeviceGroupManagerTest : public zxtest::Test {
 public:
  void SetUp() override { device_group_manager_ = std::make_unique<DeviceGroupManager>(&bridge_); }

  fit::result<fuchsia_driver_framework::DeviceGroupError> AddDeviceGroup(
      fuchsia_driver_framework::wire::DeviceGroup group_info) {
    auto device_group = std::make_unique<FakeDeviceGroup>(DeviceGroupCreateInfo{
        .topological_path = std::string(group_info.topological_path().get()),
        .size = group_info.nodes().count(),
    });
    return device_group_manager_->AddDeviceGroup(group_info, std::move(device_group));
  }

  std::unique_ptr<DeviceGroupManager> device_group_manager_;
  FakeDeviceManagerBridge bridge_;
};

TEST_F(DeviceGroupManagerTest, TestAddMatchDeviceGroup) {
  fidl::Arena allocator;

  fidl::VectorView<fdf::wire::BindRule> bind_rules_1(allocator, 1);
  auto prop_vals_1 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 1);
  prop_vals_1[0] = fdf::wire::NodePropertyValue::WithIntValue(10);
  bind_rules_1[0] = fdf::wire::BindRule{
      .key = fdf::wire::NodePropertyKey::WithIntValue(1),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_1,
  };

  fidl::VectorView<fdf::wire::NodeProperty> bind_props_1(allocator, 1);
  bind_props_1[0] = fdf::wire::NodeProperty::Builder(allocator)
                        .key(fdf::wire::NodePropertyKey::WithIntValue(1))
                        .value(fdf::wire::NodePropertyValue::WithIntValue(1))
                        .Build();

  fidl::VectorView<fdf::wire::BindRule> bind_rules_2(allocator, 2);
  auto prop_vals_2 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 2);
  prop_vals_2[0] = fdf::wire::NodePropertyValue::WithIntValue(10);
  bind_rules_2[0] = fdf::wire::BindRule{
      .key = fdf::wire::NodePropertyKey::WithIntValue(1),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_2,
  };

  fidl::VectorView<fdf::wire::NodeProperty> bind_props_2(allocator, 1);
  bind_props_2[0] = fdf::wire::NodeProperty::Builder(allocator)
                        .key(fdf::wire::NodePropertyKey::WithIntValue(10))
                        .value(fdf::wire::NodePropertyValue::WithIntValue(1))
                        .Build();

  fidl::VectorView<fdf::wire::DeviceGroupNode> nodes(allocator, 2);
  nodes[0] = fdf::wire::DeviceGroupNode{
      .bind_rules = bind_rules_1,
      .bind_properties = bind_props_1,
  };
  nodes[1] = fdf::wire::DeviceGroupNode{
      .bind_rules = bind_rules_2,
      .bind_properties = bind_props_2,
  };

  auto device_group_path = "/test/path";
  fdi::MatchedCompositeInfo composite_match{{
      .composite_name = "ovenbird",
  }};
  fdi::MatchedDeviceGroupInfo match{
      {.composite = composite_match, .node_names = {{"node-0", "node-1"}}}};

  bridge_.AddDeviceGroupMatch(device_group_path, match);
  ASSERT_TRUE(AddDeviceGroup(fdf::wire::DeviceGroup::Builder(allocator)
                                 .topological_path(fidl::StringView(allocator, device_group_path))
                                 .nodes(std::move(nodes))
                                 .Build())
                  .is_ok());
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
      .composite = composite_match,
      .num_nodes = 2,
      .node_names = {{"node-0", "node-1"}},
  }});

  ASSERT_EQ(std::nullopt, device_group_manager_
                              ->BindDeviceGroupNode(fidl::ToWire(allocator, matched_node_2),
                                                    std::weak_ptr<dfv2::Node>())
                              .value());
  ASSERT_TRUE(
      device_group_manager_->device_groups().at(device_group_path)->device_group_nodes()[1]);

  //  Bind device group node 1.
  auto matched_node_1 = fdi::MatchedDeviceGroupNodeInfo{{
      .device_groups = std::vector<fdi::MatchedDeviceGroupInfo>(),
  }};
  matched_node_1.device_groups()->push_back(fdi::MatchedDeviceGroupInfo{{
      .topological_path = device_group_path,
      .node_index = 0,
      .composite = composite_match,
      .num_nodes = 2,
      .node_names = {{"node-0", "node-1"}},
  }});

  ASSERT_OK(device_group_manager_->BindDeviceGroupNode(fidl::ToWire(allocator, matched_node_1),
                                                       std::weak_ptr<dfv2::Node>()));
  ASSERT_TRUE(
      device_group_manager_->device_groups().at(device_group_path)->device_group_nodes()[0]);
}

TEST_F(DeviceGroupManagerTest, TestBindSameNodeTwice) {
  fidl::Arena allocator;

  fidl::VectorView<fdf::wire::BindRule> bind_rules_1(allocator, 1);
  auto prop_vals_1 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 1);
  prop_vals_1[0] = fdf::wire::NodePropertyValue::WithIntValue(10);
  bind_rules_1[0] = fdf::wire::BindRule{
      .key = fdf::wire::NodePropertyKey::WithIntValue(1),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_1,
  };

  fidl::VectorView<fdf::wire::NodeProperty> bind_props_1(allocator, 1);
  bind_props_1[0] = fdf::wire::NodeProperty::Builder(allocator)
                        .key(fdf::wire::NodePropertyKey::WithIntValue(1))
                        .value(fdf::wire::NodePropertyValue::WithIntValue(1))
                        .Build();

  fidl::VectorView<fdf::wire::BindRule> bind_rules_2(allocator, 2);
  auto prop_vals_2 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 2);
  prop_vals_2[0] = fdf::wire::NodePropertyValue::WithIntValue(10);
  bind_rules_2[0] = fdf::wire::BindRule{
      .key = fdf::wire::NodePropertyKey::WithIntValue(1),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_2,
  };

  fidl::VectorView<fdf::wire::NodeProperty> bind_props_2(allocator, 1);
  bind_props_2[0] = fdf::wire::NodeProperty::Builder(allocator)
                        .key(fdf::wire::NodePropertyKey::WithIntValue(20))
                        .value(fdf::wire::NodePropertyValue::WithIntValue(100))
                        .Build();

  fidl::VectorView<fdf::wire::DeviceGroupNode> nodes(allocator, 2);
  nodes[0] = fdf::wire::DeviceGroupNode{
      .bind_rules = bind_rules_1,
      .bind_properties = bind_props_1,
  };
  nodes[1] = fdf::wire::DeviceGroupNode{
      .bind_rules = bind_rules_2,
      .bind_properties = bind_props_2,
  };

  auto device_group_path = "/test/path";
  fdi::MatchedCompositeInfo composite_match{{
      .composite_name = "ovenbird",
  }};
  fdi::MatchedDeviceGroupInfo match{
      {.composite = composite_match, .node_names = {{"node-0", "node-1"}}}};

  bridge_.AddDeviceGroupMatch(device_group_path, match);
  ASSERT_TRUE(AddDeviceGroup(fdf::wire::DeviceGroup::Builder(allocator)
                                 .topological_path(fidl::StringView(allocator, device_group_path))
                                 .nodes(std::move(nodes))
                                 .Build())
                  .is_ok());
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
      .composite = composite_match,
      .num_nodes = 2,
      .node_names = {{"node-0", "node-1"}},
  }});

  ASSERT_OK(device_group_manager_->BindDeviceGroupNode(fidl::ToWire(allocator, matched_node),
                                                       std::weak_ptr<dfv2::Node>()));
  ASSERT_TRUE(
      device_group_manager_->device_groups().at(device_group_path)->device_group_nodes()[0]);

  // Bind the same node.
  ASSERT_EQ(ZX_ERR_NOT_FOUND, device_group_manager_
                                  ->BindDeviceGroupNode(fidl::ToWire(allocator, matched_node),
                                                        std::weak_ptr<dfv2::Node>())
                                  .status_value());
}

TEST_F(DeviceGroupManagerTest, TestMultibind) {
  fidl::Arena allocator;

  // Add the first device group.
  fidl::VectorView<fdf::wire::BindRule> bind_rules_1(allocator, 1);
  auto prop_vals_1 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 1);
  prop_vals_1[0] = fdf::wire::NodePropertyValue::WithIntValue(10);
  bind_rules_1[0] = fdf::wire::BindRule{
      .key = fdf::wire::NodePropertyKey::WithIntValue(1),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_1,
  };

  fidl::VectorView<fdf::wire::NodeProperty> bind_props_1(allocator, 1);
  bind_props_1[0] = fdf::wire::NodeProperty::Builder(allocator)
                        .key(fdf::wire::NodePropertyKey::WithIntValue(30))
                        .value(fdf::wire::NodePropertyValue::WithIntValue(1))
                        .Build();

  fidl::VectorView<fdf::wire::BindRule> bind_rules_2(allocator, 2);
  auto prop_vals_2 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 2);
  prop_vals_2[0] = fdf::wire::NodePropertyValue::WithIntValue(10);
  bind_rules_2[0] = fdf::wire::BindRule{
      .key = fdf::wire::NodePropertyKey::WithIntValue(1),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_2,
  };

  fidl::VectorView<fdf::wire::NodeProperty> bind_props_2(allocator, 1);
  bind_props_2[0] = fdf::wire::NodeProperty::Builder(allocator)
                        .key(fdf::wire::NodePropertyKey::WithIntValue(20))
                        .value(fdf::wire::NodePropertyValue::WithIntValue(10))
                        .Build();

  fidl::VectorView<fdf::wire::DeviceGroupNode> nodes_1(allocator, 2);
  nodes_1[0] = fdf::wire::DeviceGroupNode{
      .bind_rules = bind_rules_1,
      .bind_properties = bind_props_1,
  };
  nodes_1[1] = fdf::wire::DeviceGroupNode{
      .bind_rules = bind_rules_2,
      .bind_properties = bind_props_2,
  };

  auto device_group_path_1 = "/test/path";
  auto matched_info_1 = fdi::MatchedCompositeInfo{{
      .composite_name = "waxwing",
  }};
  fdi::MatchedDeviceGroupInfo match_1{
      {.composite = matched_info_1, .node_names = {{"node-0", "node-1"}}}};

  bridge_.AddDeviceGroupMatch(device_group_path_1, match_1);
  ASSERT_TRUE(AddDeviceGroup(fdf::wire::DeviceGroup::Builder(allocator)
                                 .topological_path(fidl::StringView(allocator, device_group_path_1))
                                 .nodes(std::move(nodes_1))
                                 .Build())
                  .is_ok());
  ASSERT_EQ(
      2,
      device_group_manager_->device_groups().at(device_group_path_1)->device_group_nodes().size());

  // Add a second device group with a node that's the same as one in the first device group.
  fidl::VectorView<fdf::wire::DeviceGroupNode> nodes_2(allocator, 1);
  nodes_2[0] = fdf::wire::DeviceGroupNode{
      .bind_rules = bind_rules_2,
      .bind_properties = bind_props_2,
  };

  auto device_group_path_2 = "/test/path2";
  auto matched_info_2 = fdi::MatchedCompositeInfo{{
      .composite_name = "grosbeak",
  }};
  fdi::MatchedDeviceGroupInfo match_2{{.composite = matched_info_2, .node_names = {{"node-0"}}}};

  bridge_.AddDeviceGroupMatch(device_group_path_2, match_2);
  ASSERT_TRUE(AddDeviceGroup(fdf::wire::DeviceGroup::Builder(allocator)
                                 .topological_path(fidl::StringView(allocator, device_group_path_2))
                                 .nodes(std::move(nodes_2))
                                 .Build())
                  .is_ok());
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
      .composite = matched_info_1,
      .num_nodes = 2,
      .node_names = {{"node-0", "node-1"}},
  }});
  matched_node.device_groups()->push_back(fdi::MatchedDeviceGroupInfo{{
      .topological_path = device_group_path_2,
      .node_index = 0,
      .composite = matched_info_2,
      .num_nodes = 1,
      .node_names = {{"node-0"}},
  }});

  ASSERT_OK(device_group_manager_->BindDeviceGroupNode(fidl::ToWire(allocator, matched_node),
                                                       std::weak_ptr<dfv2::Node>()));
  ASSERT_TRUE(
      device_group_manager_->device_groups().at(device_group_path_1)->device_group_nodes()[1]);
  ASSERT_FALSE(
      device_group_manager_->device_groups().at(device_group_path_2)->device_group_nodes()[0]);

  // Bind the node again. Both device groups should now have the bound node.
  ASSERT_OK(device_group_manager_->BindDeviceGroupNode(fidl::ToWire(allocator, matched_node),
                                                       std::weak_ptr<dfv2::Node>()));
  ASSERT_TRUE(
      device_group_manager_->device_groups().at(device_group_path_1)->device_group_nodes()[1]);
  ASSERT_TRUE(
      device_group_manager_->device_groups().at(device_group_path_2)->device_group_nodes()[0]);
}

TEST_F(DeviceGroupManagerTest, TestBindWithNoCompositeMatch) {
  fidl::Arena allocator;

  fidl::VectorView<fdf::wire::BindRule> bind_rules_1(allocator, 1);
  auto prop_vals_1 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 1);
  prop_vals_1[0] = fdf::wire::NodePropertyValue::WithIntValue(10);
  bind_rules_1[0] = fdf::wire::BindRule{
      .key = fdf::wire::NodePropertyKey::WithIntValue(1),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_1,
  };

  fidl::VectorView<fdf::wire::NodeProperty> bind_props_1(allocator, 1);
  bind_props_1[0] = fdf::wire::NodeProperty::Builder(allocator)
                        .key(fdf::wire::NodePropertyKey::WithIntValue(1))
                        .value(fdf::wire::NodePropertyValue::WithIntValue(1))
                        .Build();

  fidl::VectorView<fdf::wire::BindRule> bind_rules_2(allocator, 2);
  auto prop_vals_2 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 2);
  prop_vals_2[0] = fdf::wire::NodePropertyValue::WithIntValue(10);
  bind_rules_2[0] = fdf::wire::BindRule{
      .key = fdf::wire::NodePropertyKey::WithIntValue(1),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_2,
  };

  fidl::VectorView<fdf::wire::NodeProperty> bind_props_2(allocator, 1);
  bind_props_2[0] = fdf::wire::NodeProperty::Builder(allocator)
                        .key(fdf::wire::NodePropertyKey::WithIntValue(10))
                        .value(fdf::wire::NodePropertyValue::WithIntValue(1))
                        .Build();

  fidl::VectorView<fdf::wire::DeviceGroupNode> nodes(allocator, 2);
  nodes[0] = fdf::wire::DeviceGroupNode{
      .bind_rules = bind_rules_1,
      .bind_properties = bind_props_1,
  };
  nodes[1] = fdf::wire::DeviceGroupNode{
      .bind_rules = bind_rules_2,
      .bind_properties = bind_props_2,
  };

  auto device_group_path = "/test/path";
  auto device_group = fdf::wire::DeviceGroup::Builder(allocator)
                          .topological_path(fidl::StringView(allocator, device_group_path))
                          .nodes(std::move(nodes))
                          .Build();
  ASSERT_TRUE(AddDeviceGroup(device_group).is_ok());
  ASSERT_TRUE(device_group_manager_->device_groups().at(device_group_path));

  //  Bind device group node 1.
  auto matched_node = fdi::MatchedDeviceGroupNodeInfo{{
      .device_groups = std::vector<fdi::MatchedDeviceGroupInfo>(),
  }};
  matched_node.device_groups()->push_back(fdi::MatchedDeviceGroupInfo{{
      .topological_path = device_group_path,
      .node_index = 0,
      .num_nodes = 2,
      .node_names = {{"node-0", "node-1"}},
  }});
  ASSERT_EQ(ZX_ERR_NOT_FOUND, device_group_manager_
                                  ->BindDeviceGroupNode(fidl::ToWire(allocator, matched_node),
                                                        std::weak_ptr<dfv2::Node>())
                                  .status_value());

  // Add a composite match into the matched node info.
  // Reattempt binding the device group node 1. With a matched composite driver, it should
  // now bind successfully.
  fdi::MatchedCompositeInfo composite_match{{
      .composite_name = "waxwing",
      .node_index = 1,
      .num_nodes = 2,
      .node_names = {{"node-0", "node-1"}},
  }};
  auto matched_node_with_composite = fdi::MatchedDeviceGroupNodeInfo{{
      .device_groups = std::vector<fdi::MatchedDeviceGroupInfo>(),
  }};
  matched_node_with_composite.device_groups()->push_back(fdi::MatchedDeviceGroupInfo{{
      .topological_path = device_group_path,
      .node_index = 0,
      .composite = composite_match,
      .num_nodes = 2,
      .node_names = {{"node-0", "node-1"}},
  }});
  ASSERT_OK(device_group_manager_->BindDeviceGroupNode(
      fidl::ToWire(allocator, matched_node_with_composite), std::weak_ptr<dfv2::Node>()));
  ASSERT_EQ(
      2, device_group_manager_->device_groups().at(device_group_path)->device_group_nodes().size());
  ASSERT_TRUE(
      device_group_manager_->device_groups().at(device_group_path)->device_group_nodes()[0]);
}

TEST_F(DeviceGroupManagerTest, TestAddDuplicate) {
  fidl::Arena allocator;

  fidl::VectorView<fdf::wire::BindRule> bind_rules_1(allocator, 1);
  auto prop_vals_1 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 1);
  prop_vals_1[0] = fdf::wire::NodePropertyValue::WithIntValue(10);
  bind_rules_1[0] = fdf::wire::BindRule{
      .key = fdf::wire::NodePropertyKey::WithIntValue(1),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_1,
  };

  fidl::VectorView<fdf::wire::NodeProperty> bind_props_1(allocator, 1);
  bind_props_1[0] = fdf::wire::NodeProperty::Builder(allocator)
                        .key(fdf::wire::NodePropertyKey::WithIntValue(1))
                        .value(fdf::wire::NodePropertyValue::WithIntValue(1))
                        .Build();

  fidl::VectorView<fdf::wire::DeviceGroupNode> nodes(allocator, 1);
  nodes[0] = fdf::wire::DeviceGroupNode{
      .bind_rules = bind_rules_1,
      .bind_properties = bind_props_1,
  };

  fidl::VectorView<fdf::wire::DeviceGroupNode> nodes_2(allocator, 1);
  nodes_2[0] = fdf::wire::DeviceGroupNode{
      .bind_rules = bind_rules_1,
      .bind_properties = bind_props_1,
  };

  auto device_group_path = "/test/path";
  bridge_.AddDeviceGroupMatch(device_group_path,
                              fdi::MatchedDeviceGroupInfo{{.composite = fdi::MatchedCompositeInfo{{
                                                               .composite_name = "grosbeak",
                                                           }},
                                                           .node_names = {{"node-0"}}}});

  auto device_group = fdf::wire::DeviceGroup::Builder(allocator)
                          .topological_path(fidl::StringView(allocator, device_group_path))
                          .nodes(std::move(nodes))
                          .Build();
  ASSERT_TRUE(AddDeviceGroup(device_group).is_ok());

  auto device_group_2 = fdf::wire::DeviceGroup::Builder(allocator)
                            .topological_path(fidl::StringView(allocator, device_group_path))
                            .nodes(std::move(nodes_2))
                            .Build();
  ASSERT_EQ(fuchsia_driver_framework::DeviceGroupError::kAlreadyExists,
            AddDeviceGroup(device_group_2).error_value());
}

TEST_F(DeviceGroupManagerTest, TestRebindCompositeMatch) {
  fidl::Arena allocator;

  fidl::VectorView<fdf::wire::BindRule> bind_rules_1(allocator, 1);
  auto prop_vals_1 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 1);
  prop_vals_1[0] = fdf::wire::NodePropertyValue::WithIntValue(10);
  bind_rules_1[0] = fdf::wire::BindRule{
      .key = fdf::wire::NodePropertyKey::WithIntValue(1),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_1,
  };

  fidl::VectorView<fdf::wire::NodeProperty> bind_props_1(allocator, 1);
  bind_props_1[0] = fdf::wire::NodeProperty::Builder(allocator)
                        .key(fdf::wire::NodePropertyKey::WithIntValue(1))
                        .value(fdf::wire::NodePropertyValue::WithIntValue(1))
                        .Build();

  fidl::VectorView<fdf::wire::BindRule> bind_rules_2(allocator, 2);
  auto prop_vals_2 = fidl::VectorView<fdf::wire::NodePropertyValue>(allocator, 2);
  prop_vals_2[0] = fdf::wire::NodePropertyValue::WithIntValue(10);
  bind_rules_2[0] = fdf::wire::BindRule{
      .key = fdf::wire::NodePropertyKey::WithIntValue(1),
      .condition = fdf::wire::Condition::kAccept,
      .values = prop_vals_2,
  };

  fidl::VectorView<fdf::wire::NodeProperty> bind_props_2(allocator, 1);
  bind_props_2[0] = fdf::wire::NodeProperty::Builder(allocator)
                        .key(fdf::wire::NodePropertyKey::WithIntValue(100))
                        .value(fdf::wire::NodePropertyValue::WithIntValue(10))
                        .Build();

  fidl::VectorView<fdf::wire::DeviceGroupNode> nodes(allocator, 2);
  nodes[0] = fdf::wire::DeviceGroupNode{
      .bind_rules = bind_rules_1,
      .bind_properties = bind_props_1,
  };
  nodes[1] = fdf::wire::DeviceGroupNode{
      .bind_rules = bind_rules_2,
      .bind_properties = bind_props_2,
  };

  auto device_group_path = "/test/path";
  fdi::MatchedCompositeInfo composite_match{{
      .composite_name = "ovenbird",
  }};
  fdi::MatchedDeviceGroupInfo match{
      {.composite = composite_match, .node_names = {{"node-0", "node-1"}}}};

  bridge_.AddDeviceGroupMatch(device_group_path, match);

  auto device_group = fdf::wire::DeviceGroup::Builder(allocator)
                          .topological_path(fidl::StringView(allocator, device_group_path))
                          .nodes(std::move(nodes))
                          .Build();
  ASSERT_TRUE(AddDeviceGroup(device_group).is_ok());
  ASSERT_EQ(
      2, device_group_manager_->device_groups().at(device_group_path)->device_group_nodes().size());

  ASSERT_EQ(fuchsia_driver_framework::DeviceGroupError::kAlreadyExists,
            AddDeviceGroup(device_group).error_value());
}
