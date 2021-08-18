// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/global_topology_data.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ui/scenic/lib/utils/helpers.h"

using flatland::TransformGraph;
using flatland::TransformHandle;

namespace {

constexpr TransformHandle::InstanceId kLinkInstanceId = 0;

// Gets the test-standard link handle to link to a graph rooted at |instance_id:0|.
TransformHandle GetLinkHandle(uint64_t instance_id) { return {kLinkInstanceId, instance_id}; }

// Creates a link in |links| to the the graph rooted at |instance_id:0|.
void MakeLink(flatland::GlobalTopologyData::LinkTopologyMap& links, uint64_t instance_id) {
  links[GetLinkHandle(instance_id)] = {instance_id, 0};
}

}  // namespace

namespace flatland {
namespace test {

// This is a macro so that, if the various test macros fail, we get a line number associated with a
// particular call in a unit test.
//
// |data| is a :GlobalTopologyData object. |link_id| is the instance ID for link handles.
#define CHECK_GLOBAL_TOPOLOGY_DATA(data, link_id)    \
  {                                                  \
    std::unordered_set<TransformHandle> all_handles; \
    for (auto handle : data.topology_vector) {       \
      all_handles.insert(handle);                    \
      EXPECT_NE(handle.GetInstanceId(), link_id);    \
    }                                                \
    EXPECT_EQ(all_handles, data.live_handles);       \
  }

TEST(GlobalTopologyDataTest, GlobalTopologyUnknownGraph) {
  TransformHandle unknown_handle = {1, 1};

  auto output =
      GlobalTopologyData::ComputeGlobalTopologyData({}, {}, kLinkInstanceId, unknown_handle);
  EXPECT_TRUE(output.topology_vector.empty());
  EXPECT_TRUE(output.child_counts.empty());
  EXPECT_TRUE(output.parent_indices.empty());
  EXPECT_TRUE(output.live_handles.empty());
}

TEST(GlobalTopologyDataTest, GlobalTopologyLinkExpansion) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::LinkTopologyMap links;

  auto link_2 = GetLinkHandle(2);

  TransformGraph::TopologyVector vectors[] = {{{{1, 0}, 1}, {link_2, 0}},  // 1:0 - 0:2
                                              {{{2, 0}, 0}}};              // 2:0

  MakeLink(links, 2);  // 0:2 - 2:0

  for (const auto& v : vectors) {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = v;
    uber_structs[v[0].handle.GetInstanceId()] = std::move(uber_struct);
  }

  // Combined, the global vector looks like this (the link handle is ommitted):
  //
  // 1:0 - 2:0
  GlobalTopologyData::TopologyVector expected_topology = {{1, 0}, {2, 0}};
  GlobalTopologyData::ChildCountVector expected_child_counts = {1, 0};
  GlobalTopologyData::ParentIndexVector expected_parent_indices = {0, 0};

  auto output =
      GlobalTopologyData::ComputeGlobalTopologyData(uber_structs, links, kLinkInstanceId, {1, 0});
  CHECK_GLOBAL_TOPOLOGY_DATA(output, 0u);

  EXPECT_THAT(output.topology_vector, ::testing::ElementsAreArray(expected_topology));
  EXPECT_THAT(output.child_counts, ::testing::ElementsAreArray(expected_child_counts));
  EXPECT_THAT(output.parent_indices, ::testing::ElementsAreArray(expected_parent_indices));
}

TEST(GlobalTopologyDataTest, GlobalTopologyIncompleteLink) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::LinkTopologyMap links;

  auto link_2 = GetLinkHandle(2);

  // The link is in the middle of the topology to demonstrate that the topology it links to replaces
  // it in the correct order.
  TransformGraph::TopologyVector vectors[] = {
      {{{1, 0}, 3}, {{1, 1}, 0}, {link_2, 0}, {{1, 2}, 0}},  // 1:0 - 1:1
                                                             //   \ \
                                                             //    \  0:2
                                                             //     \
                                                             //       1:2
                                                             //
      {{{2, 0}, 1}, {{2, 1}, 0}}};                           // 2:0 - 2:1

  // With only the first vector updated, we get the same result as the original topology, excluding
  // the link handle.
  //
  // 1:0 - 1:1
  //     \
  //       1:2
  GlobalTopologyData::TopologyVector expected_topology = {{1, 0}, {1, 1}, {1, 2}};
  GlobalTopologyData::ChildCountVector expected_child_counts = {2, 0, 0};
  GlobalTopologyData::ParentIndexVector expected_parent_indices = {0, 0, 0};

  auto uber_struct = std::make_unique<UberStruct>();
  uber_struct->local_topology = vectors[0];
  uber_structs[vectors[0][0].handle.GetInstanceId()] = std::move(uber_struct);

  auto output =
      GlobalTopologyData::ComputeGlobalTopologyData(uber_structs, links, kLinkInstanceId, {1, 0});
  CHECK_GLOBAL_TOPOLOGY_DATA(output, 0u);

  EXPECT_THAT(output.topology_vector, ::testing::ElementsAreArray(expected_topology));
  EXPECT_THAT(output.child_counts, ::testing::ElementsAreArray(expected_child_counts));
  EXPECT_THAT(output.parent_indices, ::testing::ElementsAreArray(expected_parent_indices));

  // With the second vector updated, we still get the same result because the two are not linked.
  //
  // 1:0 - 1:1
  //     \
  //       1:2
  uber_struct = std::make_unique<UberStruct>();
  uber_struct->local_topology = vectors[1];
  uber_structs[vectors[1][0].handle.GetInstanceId()] = std::move(uber_struct);

  output =
      GlobalTopologyData::ComputeGlobalTopologyData(uber_structs, links, kLinkInstanceId, {1, 0});
  CHECK_GLOBAL_TOPOLOGY_DATA(output, 0u);

  EXPECT_THAT(output.topology_vector, ::testing::ElementsAreArray(expected_topology));
  EXPECT_THAT(output.child_counts, ::testing::ElementsAreArray(expected_child_counts));
  EXPECT_THAT(output.parent_indices, ::testing::ElementsAreArray(expected_parent_indices));

  // When the link becomes available, the full topology is available, excluding the link handle.
  //
  // 1:0 - 1:1
  //   \ \
  //    \  2:0 - 2:1
  //     \
  //       1:2
  expected_topology = {{1, 0}, {1, 1}, {2, 0}, {2, 1}, {1, 2}};
  expected_child_counts = {3, 0, 1, 0, 0};
  expected_parent_indices = {0, 0, 0, 2, 0};

  MakeLink(links, 2);  // 0:2 - 2:0

  output =
      GlobalTopologyData::ComputeGlobalTopologyData(uber_structs, links, kLinkInstanceId, {1, 0});
  CHECK_GLOBAL_TOPOLOGY_DATA(output, 0u);

  EXPECT_THAT(output.topology_vector, ::testing::ElementsAreArray(expected_topology));
  EXPECT_THAT(output.child_counts, ::testing::ElementsAreArray(expected_child_counts));
  EXPECT_THAT(output.parent_indices, ::testing::ElementsAreArray(expected_parent_indices));
}

TEST(GlobalTopologyDataTest, GlobalTopologyLinksMismatchedUberStruct) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::LinkTopologyMap links;

  auto link_2 = GetLinkHandle(2);

  TransformGraph::TopologyVector vectors[] = {{{{1, 0}, 1}, {link_2, 0}},  // 1:0 - 0:2
                                                                           //
                                              {{{2, 0}, 0}}};              // 2:0

  // Explicitly make an incorrect link for 0:2 to 2:1, which is not the start of the topology vector
  // for instance ID 2. The link is skipped, leaving the expected topology as just 1:0.
  links[{0, 2}] = {2, 1};  // 0:2 - 2:1

  for (const auto& v : vectors) {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = v;
    uber_structs[v[0].handle.GetInstanceId()] = std::move(uber_struct);
  }

  GlobalTopologyData::TopologyVector expected_topology = {{1, 0}};
  GlobalTopologyData::ChildCountVector expected_child_counts = {0};
  GlobalTopologyData::ParentIndexVector expected_parent_indices = {0};

  auto output =
      GlobalTopologyData::ComputeGlobalTopologyData(uber_structs, links, kLinkInstanceId, {1, 0});
  CHECK_GLOBAL_TOPOLOGY_DATA(output, 0u);

  EXPECT_THAT(output.topology_vector, ::testing::ElementsAreArray(expected_topology));
  EXPECT_THAT(output.child_counts, ::testing::ElementsAreArray(expected_child_counts));
  EXPECT_THAT(output.parent_indices, ::testing::ElementsAreArray(expected_parent_indices));

  // Changing the link to the right root handle of 2:0 completes the topology.
  MakeLink(links, 2);  // 0:2 - 2:0

  // So the expected topology, excluding the link handle:
  //
  // 1:0 - 2:0
  expected_topology = {{1, 0}, {2, 0}};
  expected_child_counts = {1, 0};
  expected_parent_indices = {0, 0};

  output =
      GlobalTopologyData::ComputeGlobalTopologyData(uber_structs, links, kLinkInstanceId, {1, 0});
  CHECK_GLOBAL_TOPOLOGY_DATA(output, 0u);

  EXPECT_THAT(output.topology_vector, ::testing::ElementsAreArray(expected_topology));
  EXPECT_THAT(output.child_counts, ::testing::ElementsAreArray(expected_child_counts));
  EXPECT_THAT(output.parent_indices, ::testing::ElementsAreArray(expected_parent_indices));
}

TEST(GlobalTopologyDataTest, GlobalTopologyDiamondInheritance) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::LinkTopologyMap links;

  auto link_2 = GetLinkHandle(2);
  auto link_3 = GetLinkHandle(3);

  TransformGraph::TopologyVector vectors[] = {{{{1, 0}, 2}, {link_2, 0}, {link_3, 0}},  // 1:0 - 0:2
                                                                                        //     \
                                                                                        //       0:3
                                                                                        //
                                              {{{2, 0}, 2}, {{2, 1}, 0}, {link_3, 0}},  // 2:0 - 2:1
                                                                                        //     \
                                                                                        //       0:3
                                                                                        //
                                              {{{3, 0}, 0}}};                           // 3:0

  for (const auto& v : vectors) {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = v;
    uber_structs[v[0].handle.GetInstanceId()] = std::move(uber_struct);
  }

  MakeLink(links, 2);  // 0:2 - 2:0
  MakeLink(links, 3);  // 0:3 - 3:0

  // When fully combined, we expect to find two copies of the third subgraph.
  //
  // 1:0 - 2:0 - 2:1
  //    \      \
  //     \       3:0
  //      \
  //       3:0
  GlobalTopologyData::TopologyVector expected_topology = {{1, 0}, {2, 0}, {2, 1}, {3, 0}, {3, 0}};
  GlobalTopologyData::ChildCountVector expected_child_counts = {2, 2, 0, 0, 0};
  GlobalTopologyData::ParentIndexVector expected_parent_indices = {0, 0, 1, 1, 0};

  auto output =
      GlobalTopologyData::ComputeGlobalTopologyData(uber_structs, links, kLinkInstanceId, {1, 0});
  CHECK_GLOBAL_TOPOLOGY_DATA(output, 0u);

  EXPECT_THAT(output.topology_vector, ::testing::ElementsAreArray(expected_topology));
  EXPECT_THAT(output.child_counts, ::testing::ElementsAreArray(expected_child_counts));
  EXPECT_THAT(output.parent_indices, ::testing::ElementsAreArray(expected_parent_indices));
}

TEST(GlobalTopologyDataTest, ViewTreeSnapshot) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::LinkTopologyMap links;

  auto link_2 = GetLinkHandle(2);

  auto [control_ref1, view_ref1] = scenic::ViewRefPair::New();
  auto [control_ref2, view_ref2] = scenic::ViewRefPair::New();
  const zx_koid_t view_ref1_koid = utils::ExtractKoid(view_ref1);
  const zx_koid_t view_ref2_koid = utils::ExtractKoid(view_ref2);

  // Recreate the GlobalTopologyData from GlobalTopologyDataTest.GlobalTopologyIncompleteLink and
  // confirm that the correct ViewTreeSnapshot is generated.
  TransformGraph::TopologyVector vectors[] = {
      {{{1, 0}, 3}, {{1, 1}, 0}, {link_2, 0}, {{1, 2}, 0}},  // 1:0 - 1:1
                                                             //   \ \
                                                             //    \  0:2
                                                             //     \
                                                             //       1:2
                                                             //
      {{{2, 0}, 1}, {{2, 1}, 0}}};                           // 2:0 - 2:1
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[0];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref1));
    uber_structs[vectors[0][0].handle.GetInstanceId()] = std::move(uber_struct);
  }
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[1];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref2));
    uber_structs[vectors[1][0].handle.GetInstanceId()] = std::move(uber_struct);
  }

  // When the link becomes available, the full topology is available, excluding the link handle.
  //
  // 1:0 - 1:1
  //   \ \
  //    \  2:0 - 2:1
  //     \
  //       1:2
  GlobalTopologyData::TopologyVector expected_topology = {{1, 0}, {1, 1}, {2, 0}, {2, 1}, {1, 2}};
  GlobalTopologyData::ChildCountVector expected_child_counts = {3, 0, 1, 0, 0};
  GlobalTopologyData::ParentIndexVector expected_parent_indices = {0, 0, 0, 2, 0};

  MakeLink(links, 2);  // 0:2 - 2:0

  auto gtd =
      GlobalTopologyData::ComputeGlobalTopologyData(uber_structs, links, kLinkInstanceId, {1, 0});
  CHECK_GLOBAL_TOPOLOGY_DATA(gtd, 0u);

  EXPECT_THAT(gtd.topology_vector, ::testing::ElementsAreArray(expected_topology));
  EXPECT_THAT(gtd.child_counts, ::testing::ElementsAreArray(expected_child_counts));
  EXPECT_THAT(gtd.parent_indices, ::testing::ElementsAreArray(expected_parent_indices));

  // Since the global topology is only 2 instances, we should only see two views: the root and the
  // child, one a child of the other.
  {
    const float kWidth = 10;
    const float kHeight = 5;
    auto snapshot = gtd.GenerateViewTreeSnapshot(kWidth, kHeight);
    auto& [root, view_tree, unconnected_views, hit_tester, tree_boundaries] = snapshot;
    EXPECT_EQ(root, view_ref1_koid);
    EXPECT_EQ(view_tree.size(), 2u);

    {
      ASSERT_TRUE(view_tree.count(view_ref1_koid) == 1);
      const auto& node1 = view_tree.at(view_ref1_koid);
      EXPECT_EQ(node1.parent, ZX_KOID_INVALID);
      EXPECT_THAT(node1.children, testing::UnorderedElementsAre(view_ref2_koid));
      EXPECT_THAT(node1.bounding_box.min, testing::ElementsAre(0, 0));
      EXPECT_THAT(node1.bounding_box.max, testing::ElementsAre(kWidth, kHeight));
    }

    {
      ASSERT_TRUE(view_tree.count(view_ref2_koid) == 1);
      const auto& node2 = view_tree.at(view_ref2_koid);
      EXPECT_EQ(node2.parent, view_ref1_koid);
      EXPECT_TRUE(node2.children.empty());
      EXPECT_THAT(node2.bounding_box.min, testing::ElementsAre(0, 0));
      EXPECT_THAT(node2.bounding_box.max, testing::ElementsAre(kWidth, kHeight));
    }

    EXPECT_TRUE(unconnected_views.empty());
    EXPECT_TRUE(tree_boundaries.empty());
  }
}

#undef CHECK_GLOBAL_TOPOLOGY_DATA

}  // namespace test
}  // namespace flatland
