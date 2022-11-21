// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/node_group/node_group_manager.h"

#include <lib/fit/defer.h>

#include <zxtest/zxtest.h>

#include "src/devices/bin/driver_manager/node_group/node_group.h"
#include "src/devices/bin/driver_manager/v2/node.h"

namespace fdf = fuchsia_driver_framework;
namespace fdi = fuchsia_driver_index;

class FakeNodeGroup : public NodeGroup {
 public:
  explicit FakeNodeGroup(NodeGroupCreateInfo create_info) : NodeGroup(std::move(create_info)) {}

  zx::result<std::optional<DeviceOrNode>> BindNodeImpl(
      fuchsia_driver_index::wire::MatchedNodeGroupInfo info,
      const DeviceOrNode& device_or_node) override {
    return zx::ok(std::nullopt);
  }
};

class FakeDeviceManagerBridge : public CompositeManagerBridge {
 public:
  // CompositeManagerBridge:
  void BindNodesForNodeGroups() override {}
  void AddNodeGroupToDriverIndex(fdf::wire::NodeGroup group, AddToIndexCallback callback) override {
    auto iter = node_group_matches_.find(std::string(group.name().get()));
    zx::result<fdi::DriverIndexAddNodeGroupResponse> result;
    if (iter == node_group_matches_.end()) {
      result = zx::error(ZX_ERR_NOT_FOUND);
    } else {
      auto composite = iter->second.composite();
      auto names = iter->second.node_names();
      ZX_ASSERT(composite.has_value());
      ZX_ASSERT(names.has_value());
      result = zx::ok(fdi::DriverIndexAddNodeGroupResponse(composite.value(), names.value()));
    }
    auto defer =
        fit::defer([callback = std::move(callback), result]() mutable { callback(result); });
  }

  void AddNodeGroupMatch(std::string_view name, fdi::MatchedNodeGroupInfo match) {
    node_group_matches_[std::string(name)] = std::move(match);
  }

 private:
  // Stores matches for each node group name, that get returned to the
  // AddToIndexCallback that is given in AddNodeGroupToDriverIndex.
  std::unordered_map<std::string, fdi::MatchedNodeGroupInfo> node_group_matches_;
};

class NodeGroupManagerTest : public zxtest::Test {
 public:
  void SetUp() override { node_group_manager_ = std::make_unique<NodeGroupManager>(&bridge_); }

  fit::result<fuchsia_driver_framework::NodeGroupError> AddNodeGroup(
      fuchsia_driver_framework::wire::NodeGroup group_info) {
    auto node_group = std::make_unique<FakeNodeGroup>(NodeGroupCreateInfo{
        .name = std::string(group_info.name().get()),
        .size = group_info.nodes().count(),
    });
    return node_group_manager_->AddNodeGroup(group_info, std::move(node_group));
  }

  std::unique_ptr<NodeGroupManager> node_group_manager_;
  FakeDeviceManagerBridge bridge_;
};

TEST_F(NodeGroupManagerTest, TestAddMatchNodeGroup) {
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

  fidl::VectorView<fdf::wire::NodeRepresentation> nodes(allocator, 2);
  nodes[0] = fdf::wire::NodeRepresentation{
      .bind_rules = bind_rules_1,
      .bind_properties = bind_props_1,
  };
  nodes[1] = fdf::wire::NodeRepresentation{
      .bind_rules = bind_rules_2,
      .bind_properties = bind_props_2,
  };

  auto node_group_name = "test_name";
  fdi::MatchedCompositeInfo composite_match{{
      .composite_name = "ovenbird",
  }};
  fdi::MatchedNodeGroupInfo match{
      {.composite = composite_match, .node_names = {{"node-0", "node-1"}}}};

  bridge_.AddNodeGroupMatch(node_group_name, match);
  ASSERT_TRUE(AddNodeGroup(fdf::wire::NodeGroup::Builder(allocator)
                               .name(fidl::StringView(allocator, node_group_name))
                               .nodes(std::move(nodes))
                               .Build())
                  .is_ok());
  ASSERT_EQ(2,
            node_group_manager_->node_groups().at(node_group_name)->node_representations().size());
  ASSERT_FALSE(node_group_manager_->node_groups().at(node_group_name)->node_representations()[0]);
  ASSERT_FALSE(node_group_manager_->node_groups().at(node_group_name)->node_representations()[1]);

  //  Bind node group node 2.
  auto matched_node_2 = fdi::MatchedNodeRepresentationInfo{{
      .node_groups = std::vector<fdi::MatchedNodeGroupInfo>(),
  }};
  matched_node_2.node_groups()->push_back(fdi::MatchedNodeGroupInfo{{
      .name = node_group_name,
      .node_index = 1,
      .composite = composite_match,
      .num_nodes = 2,
      .node_names = {{"node-0", "node-1"}},
  }});

  ASSERT_EQ(std::nullopt, node_group_manager_
                              ->BindNodeRepresentation(fidl::ToWire(allocator, matched_node_2),
                                                       std::weak_ptr<dfv2::Node>())
                              .value());
  ASSERT_TRUE(node_group_manager_->node_groups().at(node_group_name)->node_representations()[1]);

  //  Bind node group node 1.
  auto matched_node_1 = fdi::MatchedNodeRepresentationInfo{{
      .node_groups = std::vector<fdi::MatchedNodeGroupInfo>(),
  }};
  matched_node_1.node_groups()->push_back(fdi::MatchedNodeGroupInfo{{
      .name = node_group_name,
      .node_index = 0,
      .composite = composite_match,
      .num_nodes = 2,
      .node_names = {{"node-0", "node-1"}},
  }});

  ASSERT_OK(node_group_manager_->BindNodeRepresentation(fidl::ToWire(allocator, matched_node_1),
                                                        std::weak_ptr<dfv2::Node>()));
  ASSERT_TRUE(node_group_manager_->node_groups().at(node_group_name)->node_representations()[0]);
}

TEST_F(NodeGroupManagerTest, TestBindSameNodeTwice) {
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

  fidl::VectorView<fdf::wire::NodeRepresentation> nodes(allocator, 2);
  nodes[0] = fdf::wire::NodeRepresentation{
      .bind_rules = bind_rules_1,
      .bind_properties = bind_props_1,
  };
  nodes[1] = fdf::wire::NodeRepresentation{
      .bind_rules = bind_rules_2,
      .bind_properties = bind_props_2,
  };

  auto node_group_name = "test_name";
  fdi::MatchedCompositeInfo composite_match{{
      .composite_name = "ovenbird",
  }};
  fdi::MatchedNodeGroupInfo match{
      {.composite = composite_match, .node_names = {{"node-0", "node-1"}}}};

  bridge_.AddNodeGroupMatch(node_group_name, match);
  ASSERT_TRUE(AddNodeGroup(fdf::wire::NodeGroup::Builder(allocator)
                               .name(fidl::StringView(allocator, node_group_name))
                               .nodes(std::move(nodes))
                               .Build())
                  .is_ok());
  ASSERT_EQ(2,
            node_group_manager_->node_groups().at(node_group_name)->node_representations().size());

  ASSERT_FALSE(node_group_manager_->node_groups().at(node_group_name)->node_representations()[0]);
  ASSERT_FALSE(node_group_manager_->node_groups().at(node_group_name)->node_representations()[1]);

  //  Bind node group node 1.
  auto matched_node = fdi::MatchedNodeRepresentationInfo{{
      .node_groups = std::vector<fdi::MatchedNodeGroupInfo>(),
  }};
  matched_node.node_groups()->push_back(fdi::MatchedNodeGroupInfo{{
      .name = node_group_name,
      .node_index = 0,
      .composite = composite_match,
      .num_nodes = 2,
      .node_names = {{"node-0", "node-1"}},
  }});

  ASSERT_OK(node_group_manager_->BindNodeRepresentation(fidl::ToWire(allocator, matched_node),
                                                        std::weak_ptr<dfv2::Node>()));
  ASSERT_TRUE(node_group_manager_->node_groups().at(node_group_name)->node_representations()[0]);

  // Bind the same node.
  ASSERT_EQ(ZX_ERR_NOT_FOUND, node_group_manager_
                                  ->BindNodeRepresentation(fidl::ToWire(allocator, matched_node),
                                                           std::weak_ptr<dfv2::Node>())
                                  .status_value());
}

TEST_F(NodeGroupManagerTest, TestMultibind) {
  fidl::Arena allocator;

  // Add the first node group.
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

  fidl::VectorView<fdf::wire::NodeRepresentation> nodes_1(allocator, 2);
  nodes_1[0] = fdf::wire::NodeRepresentation{
      .bind_rules = bind_rules_1,
      .bind_properties = bind_props_1,
  };
  nodes_1[1] = fdf::wire::NodeRepresentation{
      .bind_rules = bind_rules_2,
      .bind_properties = bind_props_2,
  };

  auto node_group_name_1 = "test_name";
  auto matched_info_1 = fdi::MatchedCompositeInfo{{
      .composite_name = "waxwing",
  }};
  fdi::MatchedNodeGroupInfo match_1{
      {.composite = matched_info_1, .node_names = {{"node-0", "node-1"}}}};

  bridge_.AddNodeGroupMatch(node_group_name_1, match_1);
  ASSERT_TRUE(AddNodeGroup(fdf::wire::NodeGroup::Builder(allocator)
                               .name(fidl::StringView(allocator, node_group_name_1))
                               .nodes(std::move(nodes_1))
                               .Build())
                  .is_ok());
  ASSERT_EQ(
      2, node_group_manager_->node_groups().at(node_group_name_1)->node_representations().size());

  // Add a second node group with a node that's the same as one in the first node group.
  fidl::VectorView<fdf::wire::NodeRepresentation> nodes_2(allocator, 1);
  nodes_2[0] = fdf::wire::NodeRepresentation{
      .bind_rules = bind_rules_2,
      .bind_properties = bind_props_2,
  };

  auto node_group_name_2 = "test_name2";
  auto matched_info_2 = fdi::MatchedCompositeInfo{{
      .composite_name = "grosbeak",
  }};
  fdi::MatchedNodeGroupInfo match_2{{.composite = matched_info_2, .node_names = {{"node-0"}}}};

  bridge_.AddNodeGroupMatch(node_group_name_2, match_2);
  ASSERT_TRUE(AddNodeGroup(fdf::wire::NodeGroup::Builder(allocator)
                               .name(fidl::StringView(allocator, node_group_name_2))
                               .nodes(std::move(nodes_2))
                               .Build())
                  .is_ok());
  ASSERT_EQ(
      1, node_group_manager_->node_groups().at(node_group_name_2)->node_representations().size());

  // Bind the node that's in both device node_groups(). The node should only bind to one
  // node group.
  auto matched_node = fdi::MatchedNodeRepresentationInfo{{
      .node_groups = std::vector<fdi::MatchedNodeGroupInfo>(),
  }};
  matched_node.node_groups()->push_back(fdi::MatchedNodeGroupInfo{{
      .name = node_group_name_1,
      .node_index = 1,
      .composite = matched_info_1,
      .num_nodes = 2,
      .node_names = {{"node-0", "node-1"}},
  }});
  matched_node.node_groups()->push_back(fdi::MatchedNodeGroupInfo{{
      .name = node_group_name_2,
      .node_index = 0,
      .composite = matched_info_2,
      .num_nodes = 1,
      .node_names = {{"node-0"}},
  }});

  ASSERT_OK(node_group_manager_->BindNodeRepresentation(fidl::ToWire(allocator, matched_node),
                                                        std::weak_ptr<dfv2::Node>()));
  ASSERT_TRUE(node_group_manager_->node_groups().at(node_group_name_1)->node_representations()[1]);
  ASSERT_FALSE(node_group_manager_->node_groups().at(node_group_name_2)->node_representations()[0]);

  // Bind the node again. Both node groups should now have the bound node.
  ASSERT_OK(node_group_manager_->BindNodeRepresentation(fidl::ToWire(allocator, matched_node),
                                                        std::weak_ptr<dfv2::Node>()));
  ASSERT_TRUE(node_group_manager_->node_groups().at(node_group_name_1)->node_representations()[1]);
  ASSERT_TRUE(node_group_manager_->node_groups().at(node_group_name_2)->node_representations()[0]);
}

TEST_F(NodeGroupManagerTest, TestBindWithNoCompositeMatch) {
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

  fidl::VectorView<fdf::wire::NodeRepresentation> nodes(allocator, 2);
  nodes[0] = fdf::wire::NodeRepresentation{
      .bind_rules = bind_rules_1,
      .bind_properties = bind_props_1,
  };
  nodes[1] = fdf::wire::NodeRepresentation{
      .bind_rules = bind_rules_2,
      .bind_properties = bind_props_2,
  };

  auto node_group_name = "test_name";
  auto node_group = fdf::wire::NodeGroup::Builder(allocator)
                        .name(fidl::StringView(allocator, node_group_name))
                        .nodes(std::move(nodes))
                        .Build();
  ASSERT_TRUE(AddNodeGroup(node_group).is_ok());
  ASSERT_TRUE(node_group_manager_->node_groups().at(node_group_name));

  //  Bind node group node 1.
  auto matched_node = fdi::MatchedNodeRepresentationInfo{{
      .node_groups = std::vector<fdi::MatchedNodeGroupInfo>(),
  }};
  matched_node.node_groups()->push_back(fdi::MatchedNodeGroupInfo{{
      .name = node_group_name,
      .node_index = 0,
      .num_nodes = 2,
      .node_names = {{"node-0", "node-1"}},
  }});
  ASSERT_EQ(ZX_ERR_NOT_FOUND, node_group_manager_
                                  ->BindNodeRepresentation(fidl::ToWire(allocator, matched_node),
                                                           std::weak_ptr<dfv2::Node>())
                                  .status_value());

  // Add a composite match into the matched node info.
  // Reattempt binding the node group node 1. With a matched composite driver, it should
  // now bind successfully.
  fdi::MatchedCompositeInfo composite_match{{
      .composite_name = "waxwing",
      .node_index = 1,
      .num_nodes = 2,
      .node_names = {{"node-0", "node-1"}},
  }};
  auto matched_node_with_composite = fdi::MatchedNodeRepresentationInfo{{
      .node_groups = std::vector<fdi::MatchedNodeGroupInfo>(),
  }};
  matched_node_with_composite.node_groups()->push_back(fdi::MatchedNodeGroupInfo{{
      .name = node_group_name,
      .node_index = 0,
      .composite = composite_match,
      .num_nodes = 2,
      .node_names = {{"node-0", "node-1"}},
  }});
  ASSERT_OK(node_group_manager_->BindNodeRepresentation(
      fidl::ToWire(allocator, matched_node_with_composite), std::weak_ptr<dfv2::Node>()));
  ASSERT_EQ(2,
            node_group_manager_->node_groups().at(node_group_name)->node_representations().size());
  ASSERT_TRUE(node_group_manager_->node_groups().at(node_group_name)->node_representations()[0]);
}

TEST_F(NodeGroupManagerTest, TestAddDuplicate) {
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

  fidl::VectorView<fdf::wire::NodeRepresentation> nodes(allocator, 1);
  nodes[0] = fdf::wire::NodeRepresentation{
      .bind_rules = bind_rules_1,
      .bind_properties = bind_props_1,
  };

  fidl::VectorView<fdf::wire::NodeRepresentation> nodes_2(allocator, 1);
  nodes_2[0] = fdf::wire::NodeRepresentation{
      .bind_rules = bind_rules_1,
      .bind_properties = bind_props_1,
  };

  auto node_group_name = "test_name";
  bridge_.AddNodeGroupMatch(node_group_name,
                            fdi::MatchedNodeGroupInfo{{.composite = fdi::MatchedCompositeInfo{{
                                                           .composite_name = "grosbeak",
                                                       }},
                                                       .node_names = {{"node-0"}}}});

  auto node_group = fdf::wire::NodeGroup::Builder(allocator)
                        .name(fidl::StringView(allocator, node_group_name))
                        .nodes(std::move(nodes))
                        .Build();
  ASSERT_TRUE(AddNodeGroup(node_group).is_ok());

  auto node_group_2 = fdf::wire::NodeGroup::Builder(allocator)
                          .name(fidl::StringView(allocator, node_group_name))
                          .nodes(std::move(nodes_2))
                          .Build();
  ASSERT_EQ(fuchsia_driver_framework::NodeGroupError::kAlreadyExists,
            AddNodeGroup(node_group_2).error_value());
}

TEST_F(NodeGroupManagerTest, TestRebindCompositeMatch) {
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

  fidl::VectorView<fdf::wire::NodeRepresentation> nodes(allocator, 2);
  nodes[0] = fdf::wire::NodeRepresentation{
      .bind_rules = bind_rules_1,
      .bind_properties = bind_props_1,
  };
  nodes[1] = fdf::wire::NodeRepresentation{
      .bind_rules = bind_rules_2,
      .bind_properties = bind_props_2,
  };

  auto node_group_name = "test_name";
  fdi::MatchedCompositeInfo composite_match{{
      .composite_name = "ovenbird",
  }};
  fdi::MatchedNodeGroupInfo match{
      {.composite = composite_match, .node_names = {{"node-0", "node-1"}}}};

  bridge_.AddNodeGroupMatch(node_group_name, match);

  auto node_group = fdf::wire::NodeGroup::Builder(allocator)
                        .name(fidl::StringView(allocator, node_group_name))
                        .nodes(std::move(nodes))
                        .Build();
  ASSERT_TRUE(AddNodeGroup(node_group).is_ok());
  ASSERT_EQ(2,
            node_group_manager_->node_groups().at(node_group_name)->node_representations().size());

  ASSERT_EQ(fuchsia_driver_framework::NodeGroupError::kAlreadyExists,
            AddNodeGroup(node_group).error_value());
}
