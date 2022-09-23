// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/global_topology_data.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ui/scenic/lib/flatland/global_matrix_data.h"
#include "src/ui/scenic/lib/flatland/uber_struct_system.h"
#include "src/ui/scenic/lib/utils/helpers.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_transform_2d.hpp>

using flatland::TransformGraph;
using flatland::TransformHandle;

namespace {

constexpr TransformHandle::InstanceId kLinkInstanceId = 0;
constexpr float kEpsilon = 1e-3f;

// Gets the test-standard link handle to link to a graph rooted at |instance_id:0|.
TransformHandle GetInternalLinkHandle(uint64_t instance_id) {
  return {kLinkInstanceId, instance_id};
}

// Creates a link in |links| to the the graph rooted at |instance_id:0|.
void MakeLink(flatland::GlobalTopologyData::LinkTopologyMap& links, uint64_t instance_id) {
  links[GetInternalLinkHandle(instance_id)] = {instance_id, 0};
}

}  // namespace

namespace flatland::test {

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

view_tree::SubtreeSnapshot GenerateSnapshot(
    const UberStruct::InstanceMap& uber_structs, const GlobalTopologyData::LinkTopologyMap& links,
    TransformHandle::InstanceId link_instance_id, TransformHandle root,
    const std::unordered_map<TransformHandle, TransformHandle>
        child_parent_viewport_watcher_mapping) {
  auto gtd =
      GlobalTopologyData::ComputeGlobalTopologyData(uber_structs, links, kLinkInstanceId, {1, 0});
  CHECK_GLOBAL_TOPOLOGY_DATA(gtd, 0u);

  const auto matrix_vector =
      flatland::ComputeGlobalMatrices(gtd.topology_vector, gtd.parent_indices, uber_structs);
  const auto global_clip_regions = ComputeGlobalTransformClipRegions(
      gtd.topology_vector, gtd.parent_indices, matrix_vector, uber_structs);
  gtd.hit_regions =
      ComputeGlobalHitRegions(gtd.topology_vector, gtd.parent_indices, matrix_vector, uber_structs);
  return GlobalTopologyData::GenerateViewTreeSnapshot(gtd, global_clip_regions, matrix_vector,
                                                      child_parent_viewport_watcher_mapping);
}

view_tree::SubtreeHitTester GenerateHitTester(
    const UberStruct::InstanceMap& uber_structs, const GlobalTopologyData::LinkTopologyMap& links,
    TransformHandle::InstanceId link_instance_id, TransformHandle root,
    const std::unordered_map<TransformHandle, TransformHandle>
        child_parent_viewport_watcher_mapping) {
  auto snapshot = GenerateSnapshot(uber_structs, links, link_instance_id, root,
                                   child_parent_viewport_watcher_mapping);
  return std::move(snapshot.hit_tester);
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

  const auto link_2 = GetInternalLinkHandle(2);

  const TransformGraph::TopologyVector vectors[] = {{{{1, 0}, 1}, {link_2, 0}},  // 1:0 - 0:2
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

  const auto link_2 = GetInternalLinkHandle(2);

  // The link is in the middle of the topology to demonstrate that the topology it links to replaces
  // it in the correct order.
  const TransformGraph::TopologyVector vectors[] = {
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

  const auto link_2 = GetInternalLinkHandle(2);

  const TransformGraph::TopologyVector vectors[] = {{{{1, 0}, 1}, {link_2, 0}},  // 1:0 - 0:2
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

  const auto link_2 = GetInternalLinkHandle(2);
  const auto link_3 = GetInternalLinkHandle(3);

  const TransformGraph::TopologyVector vectors[] = {
      {{{1, 0}, 2}, {link_2, 0}, {link_3, 0}},  // 1:0 - 0:2
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

TEST(GlobalTopologyDataTest, HitTest_OneView) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::LinkTopologyMap links;

  auto [control_ref1, view_ref1] = scenic::ViewRefPair::New();
  const zx_koid_t view_ref1_koid = utils::ExtractKoid(view_ref1);
  const uint32_t kWidth = 1, kHeight = 1;

  const TransformHandle view_ref1_root_transform = {1, 0};
  const TransformGraph::TopologyVector vectors[] = {{{view_ref1_root_transform, 0}}};  // 1:0

  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[0];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref1));
    TransformClipRegion clip_region = {.x = 0, .y = 0, .width = kWidth, .height = kHeight};
    uber_struct->local_clip_regions.try_emplace(view_ref1_root_transform, std::move(clip_region));
    uber_struct->local_hit_regions_map[view_ref1_root_transform] = {
        {.region = {0, 0, kWidth, kHeight}}};

    uber_structs[vectors[0][0].handle.GetInstanceId()] = std::move(uber_struct);
  }

  auto hit_tester = GenerateHitTester(uber_structs, links, kLinkInstanceId, {1, 0}, {});

  // Recall that the valid bounds are (0,0) to (1,1).

  // Perform several in-bounds hit tests.
  for (float x = 0; x <= kWidth; x += 0.1) {
    for (float y = 0; y <= kHeight; y += 0.1) {
      auto result = hit_tester(view_ref1_koid, {x, y}, true);
      ASSERT_EQ(result.hits.size(), 1u);
      EXPECT_EQ(result.hits[0], view_ref1_koid);
    }
  }

  // Perform several out-of-bounds hit tests.
  {
    auto result = hit_tester(view_ref1_koid, {-.1, -.1}, true);
    EXPECT_EQ(result.hits.size(), 0u);
  }
  {
    auto result = hit_tester(view_ref1_koid, {0, 1.1}, true);
    EXPECT_EQ(result.hits.size(), 0u);
  }
  {
    auto result = hit_tester(view_ref1_koid, {1.1, 0}, true);
    EXPECT_EQ(result.hits.size(), 0u);
  }
  {
    auto result = hit_tester(view_ref1_koid, {1.1, 1.1}, true);
    EXPECT_EQ(result.hits.size(), 0u);
  }

  // Perform a hit test with invalid root viewref.
  {
    auto result = hit_tester(0, {0, 0}, true);
    EXPECT_EQ(result.hits.size(), 0u);
  }
}

// Place two hit regions (1x1 each) in the view (2x1).
// The left hit region is semantically VISIBLE.
// The right hit region is semantically INVISIBLE.
// -------------------
// |Vis     |Invis   |
// |        |        |
// |        |        |
// |        |        |
// -------------------
TEST(GlobalTopologyDataTest, HitTest_SemanticVisibility) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::LinkTopologyMap links;

  auto [control_ref1, view_ref1] = scenic::ViewRefPair::New();
  const zx_koid_t view_ref1_koid = utils::ExtractKoid(view_ref1);
  const uint32_t kWidth = 2, kHeight = 1;

  const TransformHandle view_ref1_root_transform = {1, 0};
  const TransformGraph::TopologyVector vectors[] = {{{view_ref1_root_transform, 0}}};  // 1:0
  const fuchsia::math::RectF semantically_visible_hit_region = {0, 0, 1, 1};
  const fuchsia::math::RectF semantically_invisible_hit_region = {1, 0, 1, 1};

  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[0];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref1));
    TransformClipRegion clip_region = {.x = 0, .y = 0, .width = kWidth, .height = kHeight};
    uber_struct->local_clip_regions.try_emplace(view_ref1_root_transform, std::move(clip_region));
    uber_struct->local_hit_regions_map[view_ref1_root_transform] = {
        {.region = semantically_visible_hit_region,
         .hit_test = fuchsia::ui::composition::HitTestInteraction::DEFAULT},
        {.region = semantically_invisible_hit_region,
         .hit_test = fuchsia::ui::composition::HitTestInteraction::SEMANTICALLY_INVISIBLE}};

    uber_structs[vectors[0][0].handle.GetInstanceId()] = std::move(uber_struct);
  }

  auto hit_tester = GenerateHitTester(uber_structs, links, kLinkInstanceId, {1, 0}, {});

  for (float x = 0; x <= kWidth; x += 0.1) {
    for (float y = 0; y <= kHeight; y += 0.1) {
      auto default_result = hit_tester(view_ref1_koid, {x, y}, false);
      auto semantic_result = hit_tester(view_ref1_koid, {x, y}, true);

      if (utils::RectFContainsPoint(semantically_visible_hit_region, x, y)) {
        // In the semantically VISIBLE portion.
        ASSERT_EQ(default_result.hits.size(), 1u);
        EXPECT_EQ(default_result.hits[0], view_ref1_koid);

        ASSERT_EQ(semantic_result.hits.size(), 1u);
        EXPECT_EQ(semantic_result.hits[0], view_ref1_koid);
      } else if (utils::RectFContainsPoint(semantically_invisible_hit_region, x, y)) {
        // In the semantically INVISIBLE portion.
        ASSERT_EQ(default_result.hits.size(), 1u);
        EXPECT_EQ(default_result.hits[0], view_ref1_koid);

        EXPECT_EQ(semantic_result.hits.size(), 0u);
      }
    }
  }
}

// This test has one child view (1x1) stacked on top of a parent one (2x1).
// The child view only obscures half the parent one, such as in the following diagram:
// -------------------
// |P       |C       |
// |        |        |
// |        |        |
// |        |        |
// -------------------
// So the parent is hittable from (0,0) to (2,1)
// And the child is hittable from (1,0) to (2,1)
// With hits from (1,0) to (2,1) hitting the child first.
TEST(GlobalTopologyDataTest, HitTest_TwoOverlappingViews) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::LinkTopologyMap links;

  const auto link_2 = GetInternalLinkHandle(2);

  auto [control_ref1, view_ref_parent] = scenic::ViewRefPair::New();
  auto [control_ref2, view_ref_child] = scenic::ViewRefPair::New();
  const zx_koid_t view_ref_parent_koid = utils::ExtractKoid(view_ref_parent);
  const zx_koid_t view_ref_child_koid = utils::ExtractKoid(view_ref_child);
  const uint32_t kParentWidth = 2, kChildWidth = 1;
  const uint32_t kHeight = 1;

  const TransformHandle view_ref_parent_root_transform = {1, 0};
  const TransformHandle view_ref_child_root_transform = {2, 0};
  const TransformGraph::TopologyVector vectors[] = {{{{1, 0}, 1}, {link_2, 0}},  // 1:0 - 0:2
                                                                                 //
                                                    {{{2, 0}, 0}}};              // 2:0

  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[0];
    uber_struct->view_ref =
        std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref_parent));
    TransformClipRegion clip_region = {.x = 0, .y = 0, .width = kParentWidth, .height = kHeight};
    uber_struct->local_clip_regions.try_emplace(view_ref_parent_root_transform,
                                                std::move(clip_region));
    uber_struct->local_hit_regions_map[view_ref_parent_root_transform] = {
        {.region = {0, 0, kParentWidth, kHeight}}};

    uber_structs[vectors[0][0].handle.GetInstanceId()] = std::move(uber_struct);
  }
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[1];
    uber_struct->view_ref =
        std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref_child));
    uber_struct->local_hit_regions_map[view_ref_child_root_transform] = {
        {.region = {1, 0, kChildWidth, kHeight}}};

    uber_structs[vectors[1][0].handle.GetInstanceId()] = std::move(uber_struct);
  }

  MakeLink(links, 2);  // 0:2 - 2:0
  auto hit_tester = GenerateHitTester(uber_structs, links, kLinkInstanceId, {1, 0}, {});

  // Recall that the valid bounds are (0,0) to (1,1).

  // Perform several in-bounds hit tests on the region of just the parent.
  for (float x = 0; x <= kParentWidth - kChildWidth; x += 0.1) {
    for (float y = 0; y <= kHeight; y += 0.1) {
      {
        // Hit test with ViewRef 1 as the root.
        auto result = hit_tester(view_ref_parent_koid, {x, y}, true);
        ASSERT_EQ(result.hits.size(), 1u);
        EXPECT_EQ(result.hits[0], view_ref_parent_koid);
      }
      {
        // Hit test with ViewRef 2 as the root.
        auto result = hit_tester(view_ref_child_koid, {x, y}, true);
        EXPECT_EQ(result.hits.size(), 0u);
      }
    }
  }

  // Perform several in-bounds hit tests on the region of both parent and child.
  for (float x = 1; x <= kParentWidth; x += 0.1) {
    for (float y = 0; y <= kHeight; y += 0.1) {
      {
        // Hit test with ViewRef 1 as the root.
        auto result = hit_tester(view_ref_parent_koid, {x, y}, true);
        ASSERT_EQ(result.hits.size(), 2u);
        EXPECT_EQ(result.hits[0], view_ref_child_koid);
        EXPECT_EQ(result.hits[1], view_ref_parent_koid);
      }
      {
        // Hit test with ViewRef 2 as the root.
        auto result = hit_tester(view_ref_child_koid, {x, y}, true);
        ASSERT_EQ(result.hits.size(), 1u);
        EXPECT_EQ(result.hits[0], view_ref_child_koid);
      }
    }
  }

  // Perform several out-of-bounds hit tests.
  {
    auto result = hit_tester(view_ref_parent_koid, {-.1, -.1}, true);
    EXPECT_EQ(result.hits.size(), 0u);
  }
  {
    auto result = hit_tester(view_ref_parent_koid, {0, 1.1}, true);
    EXPECT_EQ(result.hits.size(), 0u);
  }
  {
    auto result = hit_tester(view_ref_parent_koid, {2.1, 0}, true);
    EXPECT_EQ(result.hits.size(), 0u);
  }
  {
    auto result = hit_tester(view_ref_parent_koid, {2.1, 1.1}, true);
    EXPECT_EQ(result.hits.size(), 0u);
  }
}

// This test has one anonymous child view with a subtree stacked on top of a named parent view.
// The anonymous child view and it's children should not receive any hits.
TEST(GlobalTopologyDataTest, HitTest_AnonymousView) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::LinkTopologyMap links;

  const auto link_2 = GetInternalLinkHandle(2);
  const auto link_3 = GetInternalLinkHandle(3);

  auto [control_ref1, view_ref1] = scenic::ViewRefPair::New();
  auto [control_ref3, view_ref3] = scenic::ViewRefPair::New();
  const zx_koid_t view_ref1_koid = utils::ExtractKoid(view_ref1);
  const zx_koid_t view_ref3_koid = utils::ExtractKoid(view_ref3);
  const uint32_t kWidth = 1, kHeight = 1;

  const TransformHandle view_ref1_root_transform = {1, 0};
  const TransformHandle view_ref2_root_transform = {2, 0};
  const TransformHandle view_ref3_root_transform = {2, 0};
  const TransformGraph::TopologyVector vectors[] = {{{{1, 0}, 1}, {link_2, 0}},  // 1:0 - 0:2
                                                    {{{2, 0}, 1}, {link_3, 0}},  // 2:0 - 0:3
                                                    {{{3, 0}, 0}}};              // 3:0

  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[0];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref1));
    TransformClipRegion clip_region = {.x = 0, .y = 0, .width = kWidth, .height = kHeight};
    uber_struct->local_clip_regions.try_emplace(view_ref1_root_transform, std::move(clip_region));
    uber_struct->local_hit_regions_map[view_ref1_root_transform] = {
        {.region = {0, 0, kWidth, kHeight}}};

    uber_structs[vectors[0][0].handle.GetInstanceId()] = std::move(uber_struct);
  }
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[1];
    uber_structs[vectors[1][0].handle.GetInstanceId()] = std::move(uber_struct);
  }
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[1];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref3));
    uber_struct->local_hit_regions_map[view_ref3_root_transform] = {
        {.region = {0, 0, kWidth, kHeight}}};
    uber_structs[vectors[2][0].handle.GetInstanceId()] = std::move(uber_struct);
  }

  MakeLink(links, 2);  // 0:2 - 2:0
  MakeLink(links, 3);  // 0:3 - 3:0
  auto hit_tester = GenerateHitTester(uber_structs, links, kLinkInstanceId, {1, 0}, {});

  // Recall that the valid bounds are (0,0) to (1,1).

  // Perform several in-bounds hit tests.
  for (float x = 0; x <= kWidth; x += 0.1) {
    for (float y = 0; y <= kHeight; y += 0.1) {
      // Hit test with ViewRef 1 as the root.
      auto result = hit_tester(view_ref1_koid, {x, y}, true);
      ASSERT_EQ(result.hits.size(), 1u);
      EXPECT_EQ(result.hits[0], view_ref1_koid);
    }
  }
}

// This test has one child view stacked on top of a parent one.
//
// The parent, however, has a hit region on top of the child view, so hits in that region should go
// to the parent, even though it is below the child.
//
// Let P_0 represent the base parent view, C_1 be the child, and P_2 be the parent's top hit region.
// ---------------------------
// |P_0     |C_1     |P_2    |
// |        |        |       |
// |        |        |       |
// |        |        |       |
// ---------------------------
// So the parent receives hits from (0,0) to (3,1)
// And the child receives hits from (1,0) to (3,1)
//
// However, the child is the top-most hit in only the second third of the image.
TEST(GlobalTopologyDataTest, HitTest_SandwichTest) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::LinkTopologyMap links;

  const auto link_2 = GetInternalLinkHandle(2);

  auto [control_ref1, view_ref1] = scenic::ViewRefPair::New();
  auto [control_ref2, view_ref2] = scenic::ViewRefPair::New();
  const zx_koid_t view_ref1_koid = utils::ExtractKoid(view_ref1);
  const zx_koid_t view_ref2_koid = utils::ExtractKoid(view_ref2);
  const uint32_t kParentWidth = 3, kChildWidth = 2;
  const uint32_t kHeight = 1;

  const TransformHandle view_ref1_root_transform = {1, 0};
  const TransformHandle view_ref2_root_transform = {2, 0};
  const TransformGraph::TopologyVector vectors[] = {
      {{{1, 0}, 2}, {link_2, 0}, {{1, 1}, 0}},  // 1:0 - 0:2
                                                //    \
                                                //    1:1
                                                //
      {{{2, 0}, 0}}};                           // 2:0

  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[0];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref1));
    TransformClipRegion clip_region = {.x = 0, .y = 0, .width = kParentWidth, .height = kHeight};
    uber_struct->local_clip_regions.try_emplace(view_ref1_root_transform, std::move(clip_region));
    uber_struct->local_hit_regions_map[view_ref1_root_transform] = {
        {.region = {0, 0, kParentWidth, kHeight}}};
    uber_struct->local_hit_regions_map[{1, 1}] = {{.region = {2, 0, 1, kHeight}}};

    uber_structs[vectors[0][0].handle.GetInstanceId()] = std::move(uber_struct);
  }
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[1];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref2));
    uber_struct->local_hit_regions_map[view_ref2_root_transform] = {{.region = {1, 0, 2, kHeight}}};

    uber_structs[vectors[1][0].handle.GetInstanceId()] = std::move(uber_struct);
  }

  MakeLink(links, 2);  // 0:2 - 2:0
  auto hit_tester = GenerateHitTester(uber_structs, links, kLinkInstanceId, {1, 0}, {});

  // Perform several in-bounds hit tests in the "sandwich" region.
  for (float x = 2; x <= 3; x += 0.1) {
    for (float y = 0; y <= kHeight; y += 0.1) {
      auto result = hit_tester(view_ref1_koid, {x, y}, true);
      ASSERT_EQ(result.hits.size(), 3u);
      EXPECT_EQ(result.hits[0], view_ref1_koid);
      EXPECT_EQ(result.hits[1], view_ref2_koid);
      EXPECT_EQ(result.hits[2], view_ref1_koid);
    }
  }
}

// Each view has a "full screen" (1x1) hit region. However, since we set start node to be a subset
// of the graph, not every view should be included in the hits vector returned by the hit_tester,
// based on |start_node|.
//
// View topology:
//
//      A
//     / \
//    B   E
//   / \
//  C   D
//
// Ex:
// start_node = B
// Any in-bounds hit should result in the following hit vector [D, C, B].
TEST(GlobalTopologyDataTest, HitTest_StartNodeTest) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::LinkTopologyMap links;

  const auto link_2 = GetInternalLinkHandle(2);
  const auto link_2_3 = GetInternalLinkHandle(3);
  const auto link_2_4 = GetInternalLinkHandle(4);
  const auto link_5 = GetInternalLinkHandle(5);

  // 1 = A, 2 = B, etc.
  auto [control_ref1, view_ref_A] = scenic::ViewRefPair::New();
  auto [control_ref2, view_ref_B] = scenic::ViewRefPair::New();
  auto [control_ref3, view_ref_C] = scenic::ViewRefPair::New();
  auto [control_ref4, view_ref_D] = scenic::ViewRefPair::New();
  auto [control_ref5, view_ref_E] = scenic::ViewRefPair::New();
  const zx_koid_t view_ref_A_koid = utils::ExtractKoid(view_ref_A);
  const zx_koid_t view_ref_B_koid = utils::ExtractKoid(view_ref_B);
  const zx_koid_t view_ref_C_koid = utils::ExtractKoid(view_ref_C);
  const zx_koid_t view_ref_D_koid = utils::ExtractKoid(view_ref_D);
  const zx_koid_t view_ref_E_koid = utils::ExtractKoid(view_ref_E);

  const uint32_t kWidth = 1;
  const uint32_t kHeight = 1;

  const TransformHandle view_ref_A_root_transform = {1, 0};
  const TransformHandle view_ref_B_root_transform = {2, 0};
  const TransformHandle view_ref_C_root_transform = {3, 0};
  const TransformHandle view_ref_D_root_transform = {4, 0};
  const TransformHandle view_ref_E_root_transform = {5, 0};
  const TransformGraph::TopologyVector vectors[] = {
      {{{1, 0}, 2}, {link_2, 0}, {link_5, 0}},      // 1:0 - 0:5
                                                    //    \
                                                    //     0:2
                                                    //
      {{{2, 0}, 2}, {link_2_3, 0}, {link_2_4, 0}},  // 2:0 - 0:4
                                                    //    \
                                                    //     0:3
                                                    //
      {{{3, 0}, 0}},                                // 3:0
      {{{4, 0}, 0}},                                // 4:0
      {{{5, 0}, 0}}                                 // 5:0
  };

  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[0];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref_A));
    TransformClipRegion clip_region = {.x = 0, .y = 0, .width = kWidth, .height = kHeight};
    uber_struct->local_clip_regions.try_emplace(view_ref_A_root_transform, std::move(clip_region));
    uber_struct->local_hit_regions_map[{1, 0}] = {{.region = {0, 0, kWidth, kHeight}}};

    uber_structs[vectors[0][0].handle.GetInstanceId()] = std::move(uber_struct);
  }
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[1];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref_B));
    uber_struct->local_hit_regions_map[{2, 0}] = {{.region = {0, 0, kWidth, kHeight}}};

    uber_structs[vectors[1][0].handle.GetInstanceId()] = std::move(uber_struct);
  }
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[2];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref_C));
    uber_struct->local_hit_regions_map[{3, 0}] = {{.region = {0, 0, kWidth, kHeight}}};

    uber_structs[vectors[2][0].handle.GetInstanceId()] = std::move(uber_struct);
  }
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[3];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref_D));
    uber_struct->local_hit_regions_map[{4, 0}] = {{.region = {0, 0, kWidth, kHeight}}};

    uber_structs[vectors[3][0].handle.GetInstanceId()] = std::move(uber_struct);
  }
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[4];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref_E));
    uber_struct->local_hit_regions_map[{5, 0}] = {{.region = {0, 0, kWidth, kHeight}}};

    uber_structs[vectors[4][0].handle.GetInstanceId()] = std::move(uber_struct);
  }

  MakeLink(links, 2);  // 0:2 - 2:0
  MakeLink(links, 3);  // 0:3 - 3:0
  MakeLink(links, 4);  // 0:4 - 4:0
  MakeLink(links, 5);  // 0:5 - 5:0

  auto hit_tester = GenerateHitTester(uber_structs, links, kLinkInstanceId, {1, 0}, {});

  {
    auto result = hit_tester(view_ref_A_koid, {0, 0}, true);
    ASSERT_EQ(result.hits.size(), 5u);
    EXPECT_EQ(result.hits[0], view_ref_E_koid);
    EXPECT_EQ(result.hits[1], view_ref_D_koid);
    EXPECT_EQ(result.hits[2], view_ref_C_koid);
    EXPECT_EQ(result.hits[3], view_ref_B_koid);
    EXPECT_EQ(result.hits[4], view_ref_A_koid);
  }
  {
    auto result = hit_tester(view_ref_B_koid, {0, 0}, true);
    ASSERT_EQ(result.hits.size(), 3u);
    EXPECT_EQ(result.hits[0], view_ref_D_koid);
    EXPECT_EQ(result.hits[1], view_ref_C_koid);
    EXPECT_EQ(result.hits[2], view_ref_B_koid);
  }
  {
    auto result = hit_tester(view_ref_C_koid, {0, 0}, true);
    ASSERT_EQ(result.hits.size(), 1u);
    EXPECT_EQ(result.hits[0], view_ref_C_koid);
  }
  {
    auto result = hit_tester(view_ref_D_koid, {0, 0}, true);
    ASSERT_EQ(result.hits.size(), 1u);
    EXPECT_EQ(result.hits[0], view_ref_D_koid);
  }
  {
    auto result = hit_tester(view_ref_E_koid, {0, 0}, true);
    ASSERT_EQ(result.hits.size(), 1u);
    EXPECT_EQ(result.hits[0], view_ref_E_koid);
  }
}

//  This test features a 10x5 parent view, with a 2x6 child view that is rotated 90 degrees.
//
//  The below image is to scale.
// ------------
// |(Parent)  |
// |          |
// |       CCCCCC
// |       CCCCCC
// |          |
// ------------
//
// The child has an infinite hit region defined, but it should only receive hits from (7,2) to
// (10,4), as the child is clipped by its and its parent's clip boundaries.
TEST(GlobalTopologyDataTest, HitTest_ClippedandRotatedChild) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::LinkTopologyMap links;

  const auto link_2 = GetInternalLinkHandle(2);

  auto [control_ref1, view_ref_parent] = scenic::ViewRefPair::New();
  auto [control_ref2, view_ref_child] = scenic::ViewRefPair::New();
  const zx_koid_t view_ref_parent_koid = utils::ExtractKoid(view_ref_parent);
  const zx_koid_t view_ref_child_koid = utils::ExtractKoid(view_ref_child);
  const uint32_t kParentWidth = 10, kParentHeight = 5;
  const int32_t kChildWidth = 6, kChildHeight = 2;
  const uint32_t kChildClippedWidth = 3;

  const int32_t kChildOriginX = 7, kChildOriginY = 2;

  const TransformHandle view_ref_parent_root_transform = {1, 0};
  const TransformHandle view_ref_child_root_transform = {2, 0};
  const TransformGraph::TopologyVector vectors[] = {{{{1, 0}, 1}, {link_2, 0}},  // 1:0 - 0:2
                                                                                 //
                                                    {{{2, 0}, 0}}};              // 2:0

  // Create the parent uber struct.
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[0];
    uber_struct->view_ref =
        std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref_parent));
    TransformClipRegion clip_region = {
        .x = 0, .y = 0, .width = kParentWidth, .height = kParentHeight};
    uber_struct->local_clip_regions[view_ref_parent_root_transform] = std::move(clip_region);
    uber_struct->local_hit_regions_map[view_ref_parent_root_transform] = {
        {.region = {0, 0, kParentWidth, kParentHeight}}};

    uber_structs[vectors[0][0].handle.GetInstanceId()] = std::move(uber_struct);
  }

  // Create the child uber struct.
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[1];
    uber_struct->view_ref =
        std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref_child));
    // Create a maximal hit region so that the clip regions are what must be respected.
    uber_struct->local_hit_regions_map[view_ref_child_root_transform] = {
        {.region = {-100, -100, 300, 300}}};

    // Apply operations on the child to move it into position.
    glm::mat3 child_matrix = glm::mat3();
    child_matrix = glm::scale(child_matrix, {1, 1});

    child_matrix = glm::rotate(
        child_matrix,
        utils::GetOrientationAngle(fuchsia::ui::composition::Orientation::CCW_90_DEGREES));

    // Translate to account for rotation.
    child_matrix = glm::translate(child_matrix, {-kChildHeight, 0});

    // Translate to move rectangle to the correct point.
    child_matrix = glm::translate(child_matrix, {-kChildOriginY - 1, kChildOriginX});

    uber_struct->local_matrices[view_ref_child_root_transform] = child_matrix;

    TransformClipRegion clip_region = {
        .x = 0, .y = 0, .width = kChildHeight, .height = kChildWidth};
    uber_struct->local_clip_regions[view_ref_child_root_transform] = std::move(clip_region);

    uber_structs[vectors[1][0].handle.GetInstanceId()] = std::move(uber_struct);
  }

  MakeLink(links, 2);  // 0:2 - 2:0
  auto hit_tester = GenerateHitTester(uber_structs, links, kLinkInstanceId, {1, 0}, {});

  // Start negative and go beyond the bounds of the parent to ensure correct clipping.
  for (float x = -10; x <= 2 * static_cast<float>(kParentWidth); x += 0.1) {
    for (float y = -10; y <= 2 * static_cast<float>(kParentHeight); y += 0.1) {
      auto result = hit_tester(view_ref_parent_koid, {x, y}, true);

      if (utils::RectFContainsPoint(
              {kChildOriginX, kChildOriginY, kChildClippedWidth, kChildHeight}, x, y)) {
        // There should be two hit regions, child on top.
        EXPECT_EQ(result.hits.size(), 2u) << "x,y: " << x << ", " << y;
        EXPECT_EQ(result.hits[0], view_ref_child_koid);
        EXPECT_EQ(result.hits[1], view_ref_parent_koid);
      } else if (utils::RectFContainsPoint({0, 0, kParentWidth, kParentHeight}, x, y)) {
        // There should be one hit region, the parent's.
        EXPECT_EQ(result.hits.size(), 1u) << "x,y: " << x << ", " << y;
        EXPECT_EQ(result.hits[0], view_ref_parent_koid);
      } else {
        // We are outside of both the parent and child.
        EXPECT_EQ(result.hits.size(), 0u) << "x,y: " << x << ", " << y;
      }
    }
  }
}

// Overview
//
// Test for clip regions that should *not* impact a given hit test. This includes:
// - clips from a view's sibling transforms
// - clips from a view's children transforms
//
//
// Topology
//
// This test contains a parent view with children transforms, as well as a child view. The children
// transforms and child view will set minimal clip regions, which should not affect the parent's
// hits.
//
// Inside the parent view, a transform's hits should not be affected by its siblings' clip regions.
//
//
// Geometry
//
// The parent, P0, is the entire 10x10 grid. P0 is the view, and P1 and P2 are child transforms in
// it.
//
// P0 has two children in its local topology, P1 and P2, depicted below. P1 is the top 10x5 of the
// grid, and P2 is a subset of P1, comprising the top-right 5x2 region of both P0 and P1.
//
// P0 has a child view C, which is the bottom 10x5.
//
// ------------------
// |P1     |P2      |
// |       |--------|
// |                |
// |                |
// ------------------
// |C               |
// |                |
// |                |
// |                |
// ------------------
//
//
// Expectations
//
// Regardless of its children's clip regions, P0 should always receive a hit because P0's default
// hit region is placed on the root transform, and thus not affected by clip boundaries on child
// transforms.
//
// C should always receive a hit when the hit is in the bottom half of the view, since P1 and P2 are
// not C's ancestors and therefore their clip regions should not impact C whatsoever.
TEST(GlobalTopologyDataTest, HitTest_NonRelevantClipRegions) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::LinkTopologyMap links;

  const auto link_2 = GetInternalLinkHandle(2);

  auto [control_ref1, view_ref_parent] = scenic::ViewRefPair::New();
  auto [control_ref2, view_ref_child] = scenic::ViewRefPair::New();
  const zx_koid_t view_ref_parent_koid = utils::ExtractKoid(view_ref_parent);
  const zx_koid_t view_ref_child_koid = utils::ExtractKoid(view_ref_child);

  const TransformClipRegion P0_clip = {.x = 0, .y = 0, .width = 10, .height = 10};
  const TransformClipRegion P1_clip = {.x = 0, .y = 0, .width = 10, .height = 5};
  const TransformClipRegion P2_clip = {.x = 5, .y = 0, .width = 5, .height = 2};
  const TransformClipRegion C_clip = {.x = 0, .y = 5, .width = 10, .height = 5};

  const TransformHandle view_ref_parent_root_transform = {1, 0};
  const TransformHandle view_ref_child_root_transform = {2, 0};
  const TransformGraph::TopologyVector vectors[] = {
      {{{1, 0}, 3}, {{1, 1}, 0}, {{1, 2}, 0}, {link_2, 0}},  // 1:0 - 1:1
                                                             //   \ \
                                                             //    \  1:2
                                                             //     \
                                                             //       0:2
                                                             //
      {{{2, 0}, 0}}};                                        // 2:0

  // Create the parent uber struct.
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[0];
    uber_struct->view_ref =
        std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref_parent));

    // Set up all clip regions.
    uber_struct->local_clip_regions[view_ref_parent_root_transform] = std::move(P0_clip);
    uber_struct->local_clip_regions[{1, 1}] = std::move(P1_clip);
    uber_struct->local_clip_regions[{1, 2}] = std::move(P2_clip);

    // Define maximal hit region on P0.
    uber_struct->local_hit_regions_map[view_ref_parent_root_transform] = {
        {.region = {0, 0, FLT_MAX, FLT_MAX}}};

    uber_structs[vectors[0][0].handle.GetInstanceId()] = std::move(uber_struct);
  }

  // Create the child uber struct.
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[1];
    uber_struct->view_ref =
        std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref_child));
    // Create a maximal hit region so that the clip regions are what must be respected.
    uber_struct->local_hit_regions_map[view_ref_child_root_transform] = {
        {.region = {0, 0, FLT_MAX, FLT_MAX}}};

    uber_struct->local_clip_regions[view_ref_child_root_transform] = std::move(C_clip);

    uber_structs[vectors[1][0].handle.GetInstanceId()] = std::move(uber_struct);
  }

  MakeLink(links, 2);  // 0:2 - 2:0
  auto hit_tester = GenerateHitTester(uber_structs, links, kLinkInstanceId, {1, 0}, {});

  for (float x = 0; x <= static_cast<float>(P0_clip.width); x += 0.1) {
    for (float y = 0; y <= static_cast<float>(P0_clip.height); y += 0.1) {
      auto result = hit_tester(view_ref_parent_koid, {x, y}, true);

      // The last (parent) elem should *always* exist, regardless of the child's clip bounds.
      ASSERT_GE(result.hits.size(), 1u);
      EXPECT_EQ(result.hits.back(), view_ref_parent_koid);

      // If we are in the child, the clip regions of the parent's siblings should not affect it.
      if (utils::RectFContainsPoint(utils::ConvertRectToRectF(C_clip), x, y)) {
        // There should be two hit regions, child on top.
        ASSERT_EQ(result.hits.size(), 2u);
        EXPECT_EQ(result.hits[0], view_ref_child_koid);
      }
    }
  }
}

// This test ensures that snapshots containing non-full screen views have correct local from world
// transforms.
//
// View topology:
//
//      A
//     / \
//    B   C
//       / \
//      D   E
//
// A is the full screen, with dimensions 20x10
// B is the left half of A, with dimensions 10x10, scaled by a 3x2 vector, rotated by 270 CCW.
// C is the right half of A, with dimensions 10x10
// D is the bottom-left quarter of C, with dimensions 5x5, scaled by a 4x3 vector.
// E is the bottom-right quarter of C, with dimensions 5x5
//
// (A)
// ---------------------------
// |B          |C            |
// |           |             |
// |           |             |
// |           |             |
// |           |-------------|
// |           |D    |E      |
// |           |     |       |
// |           |     |       |
// |           |     |       |
// ---------------------------
TEST(GlobalTopologyDataTest, PartialScreenViews_HaveCorrectTransforms) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::LinkTopologyMap links;

  const auto link_2 = GetInternalLinkHandle(2);
  const auto link_3 = GetInternalLinkHandle(3);
  const auto link_3_4 = GetInternalLinkHandle(4);
  const auto link_3_5 = GetInternalLinkHandle(5);

  auto [control_ref1, view_ref_A] = scenic::ViewRefPair::New();
  auto [control_ref2, view_ref_B] = scenic::ViewRefPair::New();
  auto [control_ref3, view_ref_C] = scenic::ViewRefPair::New();
  auto [control_ref4, view_ref_D] = scenic::ViewRefPair::New();
  auto [control_ref5, view_ref_E] = scenic::ViewRefPair::New();
  const zx_koid_t view_ref_A_koid = utils::ExtractKoid(view_ref_A);
  const zx_koid_t view_ref_B_koid = utils::ExtractKoid(view_ref_B);
  const zx_koid_t view_ref_C_koid = utils::ExtractKoid(view_ref_C);
  const zx_koid_t view_ref_D_koid = utils::ExtractKoid(view_ref_D);
  const zx_koid_t view_ref_E_koid = utils::ExtractKoid(view_ref_E);

  const uint32_t kWidth = 20;
  const uint32_t kHeight = 10;

  const TransformHandle view_ref_A_root_transform = {1, 0};
  const TransformHandle view_ref_B_root_transform = {2, 0};
  const TransformHandle view_ref_C_root_transform = {3, 0};
  const TransformHandle view_ref_D_root_transform = {4, 0};
  const TransformHandle view_ref_E_root_transform = {5, 0};
  const TransformGraph::TopologyVector vectors[] = {
      {{{1, 0}, 2}, {link_2, 0}, {link_3, 0}},      // 1:0 - 0:3
                                                    //    \
                                                    //     0:2
                                                    // 2:0
      {{{2, 0}, 0}},                                // 3:0 - 0:5
                                                    //    |
      {{{3, 0}, 2}, {link_3_4, 0}, {link_3_5, 0}},  //     0:4
      {{{4, 0}, 0}},                                // 4:0
      {{{5, 0}, 0}}                                 // 5:0
  };

  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[0];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref_A));
    TransformClipRegion clip_region = {.x = 0, .y = 0, .width = kWidth, .height = kHeight};
    uber_struct->local_clip_regions.try_emplace(view_ref_A_root_transform, std::move(clip_region));
    uber_struct->local_hit_regions_map[{1, 0}] = {{.region = {0, 0, kWidth, kHeight}}};

    glm::mat3 translation_matrix = glm::mat3();
    translation_matrix = glm::translate(translation_matrix, {0, 0});
    uber_struct->local_matrices[view_ref_A_root_transform] = translation_matrix;

    uber_structs[vectors[0][0].handle.GetInstanceId()] = std::move(uber_struct);
  }
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[1];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref_B));
    uber_struct->local_hit_regions_map[{2, 0}] = {{.region = {0, 0, kWidth, kHeight}}};

    glm::mat3 transform_matrix = glm::mat3();
    // Translate by (height, 0) to reposition, as rotation occurs around the top-left corner.
    transform_matrix = glm::translate(transform_matrix, {2, 0});
    transform_matrix = glm::rotate(transform_matrix, -glm::three_over_two_pi<float>());
    transform_matrix = glm::scale(transform_matrix, {3, 2});
    uber_struct->local_matrices[view_ref_B_root_transform] = transform_matrix;

    uber_structs[vectors[1][0].handle.GetInstanceId()] = std::move(uber_struct);
  }
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[2];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref_C));
    uber_struct->local_hit_regions_map[{3, 0}] = {{.region = {0, 0, kWidth, kHeight}}};

    glm::mat3 translation_matrix = glm::mat3();
    translation_matrix = glm::translate(translation_matrix, {10, 0});
    uber_struct->local_matrices[view_ref_C_root_transform] = translation_matrix;

    uber_structs[vectors[2][0].handle.GetInstanceId()] = std::move(uber_struct);
  }
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[3];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref_D));
    uber_struct->local_hit_regions_map[{4, 0}] = {{.region = {0, 0, kWidth, kHeight}}};

    glm::mat3 translation_matrix = glm::mat3();
    translation_matrix = glm::translate(translation_matrix, {0, 5});
    translation_matrix = glm::scale(translation_matrix, {4, 3});
    uber_struct->local_matrices[view_ref_D_root_transform] = translation_matrix;

    uber_structs[vectors[3][0].handle.GetInstanceId()] = std::move(uber_struct);
  }
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[4];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref_E));
    uber_struct->local_hit_regions_map[{5, 0}] = {{.region = {0, 0, kWidth, kHeight}}};

    glm::mat3 translation_matrix = glm::mat3();
    translation_matrix = glm::translate(translation_matrix, {5, 5});
    uber_struct->local_matrices[view_ref_E_root_transform] = translation_matrix;

    uber_structs[vectors[4][0].handle.GetInstanceId()] = std::move(uber_struct);
  }

  MakeLink(links, 2);  // 0:2 - 2:0
  MakeLink(links, 3);  // 0:3 - 3:0
  MakeLink(links, 4);  // 0:4 - 4:0
  MakeLink(links, 5);  // 0:5 - 5:0

  const auto snapshot = GenerateSnapshot(uber_structs, links, kLinkInstanceId, /*root*/ {1, 0},
                                         /*child_parent_viewport_watcher_mapping*/ {});

  int checksum = 0;

  // Iterate through snapshot and confirm |local_from_world_transform|s look good.
  for (auto const& view_kv : snapshot.view_tree) {
    auto koid = view_kv.first;
    glm::mat4 local_from_world_transform = view_kv.second.local_from_world_transform;

    // Following Scenic naming semantics, a local_from_world matrix is equivalent to a
    // world_to_local matrix. A world_to_local matrix multiplied by a world point gives you that
    // point in the local coordinate space. Another way of saying the same thing is that a
    // world_to_local matrix describes the transformation of the world in local space.
    //
    // Therefore, since we build the expected_local_from_world transform by applying commands to go
    // from the world coordinate space to the local one, that is the same as describing the
    // transformation of the local in world space, which is the *opposite* of describing the
    // transformation of the world in local space, so we must invert.
    if (koid == view_ref_A_koid) {
      glm::mat4 expected_local_from_world = glm::mat4(1.f);
      expected_local_from_world = glm::inverse(expected_local_from_world);

      for (size_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(glm::all(glm::epsilonEqual(expected_local_from_world[i],
                                               local_from_world_transform[i], kEpsilon)));
      }

      checksum |= 1 << 0;
    } else if (koid == view_ref_B_koid) {
      glm::mat4 expected_local_from_world = glm::mat4(1.f);
      expected_local_from_world = glm::translate(glm::mat4(), glm::vec3(2, 0, 0));
      expected_local_from_world = glm::rotate(expected_local_from_world,
                                              -glm::three_over_two_pi<float>(), glm::vec3(0, 0, 1));
      expected_local_from_world = glm::scale(expected_local_from_world, glm::vec3(3, 2, 1));
      expected_local_from_world = glm::inverse(expected_local_from_world);

      for (size_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(glm::all(glm::epsilonEqual(expected_local_from_world[i],
                                               local_from_world_transform[i], kEpsilon)));
      }

      checksum |= 1 << 1;
    } else if (koid == view_ref_C_koid) {
      glm::mat4 expected_local_from_world = glm::mat4(1.f);
      expected_local_from_world = glm::translate(expected_local_from_world, glm::vec3(10, 0, 0));
      expected_local_from_world = glm::inverse(expected_local_from_world);

      for (size_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(glm::all(glm::epsilonEqual(expected_local_from_world[i],
                                               local_from_world_transform[i], kEpsilon)));
      }

      checksum |= 1 << 2;
    } else if (koid == view_ref_D_koid) {
      glm::mat4 expected_local_from_world = glm::mat4(1.f);
      expected_local_from_world = glm::translate(expected_local_from_world, glm::vec3(10, 5, 0));
      expected_local_from_world = glm::scale(expected_local_from_world, glm::vec3(4, 3, 1));
      expected_local_from_world = glm::inverse(expected_local_from_world);
      for (size_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(glm::all(glm::epsilonEqual(expected_local_from_world[i],
                                               local_from_world_transform[i], kEpsilon)));
      }

      checksum |= 1 << 3;
    } else if (koid == view_ref_E_koid) {
      glm::mat4 expected_local_from_world = glm::mat4(1.f);
      expected_local_from_world = glm::translate(expected_local_from_world, glm::vec3(15, 5, 0));
      expected_local_from_world = glm::inverse(expected_local_from_world);

      for (size_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(glm::all(glm::epsilonEqual(expected_local_from_world[i],
                                               local_from_world_transform[i], kEpsilon)));
      }

      checksum |= 1 << 4;
    } else {
      ASSERT_TRUE(false);
    }
  }

  EXPECT_EQ(snapshot.view_tree.size(), 5u);
  EXPECT_EQ(checksum, 0b11111);
}

TEST(GlobalTopologyDataTest, ViewTreeSnapshot) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::LinkTopologyMap links;

  const auto link_2 = GetInternalLinkHandle(2);

  auto [control_ref1, view_ref1] = scenic::ViewRefPair::New();
  auto [control_ref2, view_ref2] = scenic::ViewRefPair::New();
  const zx_koid_t view_ref1_koid = utils::ExtractKoid(view_ref1);
  const zx_koid_t view_ref2_koid = utils::ExtractKoid(view_ref2);
  const uint32_t kWidth = 1, kHeight = 1;

  // Recreate the GlobalTopologyData from GlobalTopologyDataTest.GlobalTopologyIncompleteLink and
  // confirm that the correct ViewTreeSnapshot is generated.
  // {1:1} acts as a transform handle for the viewport.
  const TransformGraph::TopologyVector vectors[] = {
      {{{1, 0}, 2}, {{1, 1}, 1}, {link_2, 0}, {{1, 2}, 0}},  // 1:0 - 1:1 - 0:2
                                                             //   \
                                                             //    1:2
                                                             //
      {{{2, 0}, 1}, {{2, 1}, 0}}};                           // 2:0 - 2:1

  // {1,1} acts as a parent_transform_handle to {2,1} which is the child's view watcher
  // handle.
  const auto& parent_transform_handle = vectors[0][1].handle;
  const auto& child_transform_handle = vectors[1][0].handle;
  const std::unordered_map<TransformHandle, TransformHandle> child_parent_viewport_watcher_mapping =
      {{child_transform_handle, parent_transform_handle}};
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[0];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref1));
    uber_struct->debug_name = "test_instance_1";
    TransformClipRegion clip_region = {.x = 0, .y = 0, .width = kWidth, .height = kHeight};
    uber_struct->local_clip_regions.try_emplace(parent_transform_handle, std::move(clip_region));
    uber_structs[vectors[0][0].handle.GetInstanceId()] = std::move(uber_struct);
  }
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[1];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref2));
    uber_struct->debug_name = "test_instance_2";
    uber_structs[vectors[1][0].handle.GetInstanceId()] = std::move(uber_struct);
  }

  MakeLink(links, 2);  // 0:2 - 2:0

  // Since the global topology is only 2 instances, we should only see two views: the root and the
  // child, one a child of the other.
  const auto [root, view_tree, unconnected_views, _, tree_boundaries] = GenerateSnapshot(
      uber_structs, links, kLinkInstanceId, /*root*/ {1, 0}, child_parent_viewport_watcher_mapping);
  EXPECT_EQ(root, view_ref1_koid);
  EXPECT_EQ(view_tree.size(), 2u);

  {
    ASSERT_TRUE(view_tree.count(view_ref1_koid) == 1);
    const auto& node1 = view_tree.at(view_ref1_koid);
    EXPECT_EQ(node1.parent, ZX_KOID_INVALID);
    EXPECT_THAT(node1.children, testing::UnorderedElementsAre(view_ref2_koid));
    EXPECT_EQ(node1.debug_name, "test_instance_1");
  }

  {
    ASSERT_TRUE(view_tree.count(view_ref2_koid) == 1);
    const auto& node2 = view_tree.at(view_ref2_koid);
    EXPECT_EQ(node2.parent, view_ref1_koid);
    EXPECT_TRUE(node2.children.empty());
    EXPECT_THAT(node2.bounding_box.min, testing::ElementsAre(0, 0));
    EXPECT_THAT(node2.bounding_box.max, testing::ElementsAre(kWidth, kHeight));
    EXPECT_EQ(node2.debug_name, "test_instance_2");
  }

  EXPECT_TRUE(unconnected_views.empty());
  EXPECT_TRUE(tree_boundaries.empty());
}

TEST(GlobalTopologyDataTest, ViewTreeSnapshot_AnonymousView) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::LinkTopologyMap links;

  const auto link_2 = GetInternalLinkHandle(2);
  const auto link_3 = GetInternalLinkHandle(3);
  const auto link_3_4 = GetInternalLinkHandle(4);
  const auto link_3_5 = GetInternalLinkHandle(5);

  auto [control_ref1, view_ref_1] = scenic::ViewRefPair::New();
  auto [control_ref2, view_ref_2] = scenic::ViewRefPair::New();
  auto [control_ref4, view_ref_4] = scenic::ViewRefPair::New();
  auto [control_ref5, view_ref_5] = scenic::ViewRefPair::New();
  const zx_koid_t view_ref_1_koid = utils::ExtractKoid(view_ref_1);
  const zx_koid_t view_ref_2_koid = utils::ExtractKoid(view_ref_2);
  const zx_koid_t view_ref_4_koid = utils::ExtractKoid(view_ref_4);
  const zx_koid_t view_ref_5_koid = utils::ExtractKoid(view_ref_5);

  const TransformHandle view_ref_1_root_transform = {1, 0};
  const TransformHandle view_ref_2_root_transform = {2, 0};
  const TransformHandle view_ref_3_root_transform = {3, 0};
  const TransformHandle view_ref_4_root_transform = {4, 0};
  const TransformHandle view_ref_5_root_transform = {5, 0};
  const TransformGraph::TopologyVector vectors[] = {
      {{{1, 0}, 2}, {link_2, 0}, {link_3, 0}},      // 1:0 - 0:3
                                                    //    \
                                                    //     0:2
                                                    // 2:0
      {{{2, 0}, 0}},                                // 3:0 - 0:5
                                                    //    |
      {{{3, 0}, 2}, {link_3_4, 0}, {link_3_5, 0}},  //     0:4
      {{{4, 0}, 0}},                                // 4:0
      {{{5, 0}, 0}}                                 // 5:0
  };

  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[0];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref_1));
    uber_structs[vectors[0][0].handle.GetInstanceId()] = std::move(uber_struct);
  }
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[1];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref_2));
    uber_structs[vectors[1][0].handle.GetInstanceId()] = std::move(uber_struct);
  }
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[2];
    // Make View 3 anonymous.
    uber_struct->view_ref = nullptr;
    uber_structs[vectors[2][0].handle.GetInstanceId()] = std::move(uber_struct);
  }
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[3];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref_4));
    uber_structs[vectors[3][0].handle.GetInstanceId()] = std::move(uber_struct);
  }
  {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[4];
    uber_struct->view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref_5));
    uber_structs[vectors[4][0].handle.GetInstanceId()] = std::move(uber_struct);
  }

  MakeLink(links, 2);  // 0:2 - 2:0
  MakeLink(links, 3);  // 0:3 - 3:0
  MakeLink(links, 4);  // 0:4 - 4:0
  MakeLink(links, 5);  // 0:5 - 5:0

  const auto snapshot = GenerateSnapshot(uber_structs, links, kLinkInstanceId, /*root*/ {1, 0},
                                         /*child_parent_viewport_watcher_mapping*/ {});

  // View 3 is anonymous, so it and its children should not appear in the ViewTree; instead marked
  // as unconnected.
  EXPECT_EQ(snapshot.view_tree.size(), 2u);
  EXPECT_TRUE(snapshot.view_tree.count(view_ref_1_koid));
  EXPECT_TRUE(snapshot.view_tree.count(view_ref_2_koid));
  EXPECT_EQ(snapshot.unconnected_views.size(), 2u);
  EXPECT_TRUE(snapshot.unconnected_views.count(view_ref_4_koid));
  EXPECT_TRUE(snapshot.unconnected_views.count(view_ref_5_koid));
}

/// The following 3 unit tests test edgecases where there is only a single child for
/// a given transform node, and where that child is a link and there is some issue
/// with how the link is set up (e.g. missing uber struct, link not created, wrong
/// link handle provided, etc). These tests are meant to ensure that the function
/// ComputeGlobalTopologyData() properly decrements the number of child nodes that
/// a given handle has in this particular setup.

// If the link doesn't exist, skip the link handle.
TEST(GlobalTopologyDataTest, LastChildEdgeCase_NoLink) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::LinkTopologyMap links;

  const auto link_2 = GetInternalLinkHandle(2);

  // The link is the middle child in the topology.
  const TransformGraph::TopologyVector vectors[] = {{{{1, 0}, /*One too few*/ 2},
                                                     {{1, 1}, 0},
                                                     {link_2, 0},
                                                     {{1, 2}, 0}},  // 1:0   - 1:1
                                                                    //    \  - 0:2 (Broken Link)
                                                                    //     \ - 1:2
                                                                    //
                                                    {{{2, 0}, 1}, {{2, 1}, 0}}};  // 2:0 - 2:1

  // Since we are purposefully not creating the link, the global topology
  // should just be the following:
  // 1:0 - 1:1
  GlobalTopologyData::TopologyVector expected_topology = {{1, 0}, {1, 1}, {1, 2}};
  GlobalTopologyData::ChildCountVector expected_child_counts = {1, 0, 0};
  GlobalTopologyData::ParentIndexVector expected_parent_indices = {0, 0, 0};

  auto uber_struct = std::make_unique<UberStruct>();
  uber_struct->local_topology = vectors[0];
  uber_structs[vectors[0][0].handle.GetInstanceId()] = std::move(uber_struct);

  uber_struct = std::make_unique<UberStruct>();
  uber_struct->local_topology = vectors[1];
  uber_structs[vectors[1][0].handle.GetInstanceId()] = std::move(uber_struct);

  auto output =
      GlobalTopologyData::ComputeGlobalTopologyData(uber_structs, links, kLinkInstanceId, {1, 0});
  CHECK_GLOBAL_TOPOLOGY_DATA(output, 0u);

  EXPECT_THAT(output.topology_vector, ::testing::ElementsAreArray(expected_topology));
  EXPECT_THAT(output.child_counts, ::testing::ElementsAreArray(expected_child_counts));
  EXPECT_THAT(output.parent_indices, ::testing::ElementsAreArray(expected_parent_indices));
}

TEST(GlobalTopologyDataTest, LinkEdgeCaseTest2_NoUberStruct) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::LinkTopologyMap links;

  const auto link_2 = GetInternalLinkHandle(2);

  // The link is the middle child in the topology.
  const TransformGraph::TopologyVector vectors[] = {
      {{{1, 0}, /*One too few*/ 2}, {{1, 1}, 0}, {link_2, 0}, {{1, 2}, 0}},  // 1:0   - 1:1
                                                                             //    \  - 0:2
                                                                             //     \ - 1:2
                                                                             //
      {{{2, 0}, 1}, {{2, 1}, 0}}};                                           // 2:0 - 2:1

  // Explicitly make the link.
  MakeLink(links, 2);  // 0:2 - 2:0

  auto uber_struct = std::make_unique<UberStruct>();
  uber_struct->local_topology = vectors[0];
  uber_structs[vectors[0][0].handle.GetInstanceId()] = std::move(uber_struct);

  /** Specifically do not create the uber_struct for the 2nd flatland instance
    uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = vectors[1];
    uber_structs[vectors[1][0].handle.GetInstanceId()] = std::move(uber_struct);
  **/

  // Since we are purposefully not creating the second uber struct, the global topology
  // should just be the following:
  // 1:0 - 1:1
  GlobalTopologyData::TopologyVector expected_topology = {{1, 0}, {1, 1}, {1, 2}};
  GlobalTopologyData::ChildCountVector expected_child_counts = {1, 0, 0};
  GlobalTopologyData::ParentIndexVector expected_parent_indices = {0, 0, 0};

  auto output =
      GlobalTopologyData::ComputeGlobalTopologyData(uber_structs, links, kLinkInstanceId, {1, 0});
  CHECK_GLOBAL_TOPOLOGY_DATA(output, 0u);

  EXPECT_THAT(output.topology_vector, ::testing::ElementsAreArray(expected_topology));
  EXPECT_THAT(output.child_counts, ::testing::ElementsAreArray(expected_child_counts));
  EXPECT_THAT(output.parent_indices, ::testing::ElementsAreArray(expected_parent_indices));
}

TEST(GlobalTopologyDataTest, LinkEdgeCaseTest3_WrongHandle) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::LinkTopologyMap links;

  const auto link_2 = GetInternalLinkHandle(2);

  // The link is the middle child in the topology.
  const TransformGraph::TopologyVector vectors[] = {
      {{{1, 0}, /*One too few*/ 2}, {{1, 1}, 0}, {link_2, 0}, {{1, 2}, 0}},  // 1:0   - 1:1
                                                                             //    \  - 0:2
                                                                             //     \ - 1:2
                                                                             //
      {{{2, 0}, 1}, {{2, 1}, 0}}};                                           // 2:0 - 2:1

  // Explicitly make the link, but give it the wrong handle
  MakeLink(links, /*wrong*/ 3);

  auto uber_struct = std::make_unique<UberStruct>();
  uber_struct->local_topology = vectors[0];
  uber_structs[vectors[0][0].handle.GetInstanceId()] = std::move(uber_struct);

  uber_struct = std::make_unique<UberStruct>();
  uber_struct->local_topology = vectors[1];
  uber_structs[vectors[1][0].handle.GetInstanceId()] = std::move(uber_struct);

  // Since we gave the wrong link handle, the topology should just be:
  // 1:0 - 1:1
  GlobalTopologyData::TopologyVector expected_topology = {{1, 0}, {1, 1}, {1, 2}};
  GlobalTopologyData::ChildCountVector expected_child_counts = {1, 0, 0};
  GlobalTopologyData::ParentIndexVector expected_parent_indices = {0, 0, 0};

  auto output =
      GlobalTopologyData::ComputeGlobalTopologyData(uber_structs, links, kLinkInstanceId, {1, 0});
  CHECK_GLOBAL_TOPOLOGY_DATA(output, 0u);

  EXPECT_THAT(output.topology_vector, ::testing::ElementsAreArray(expected_topology));
  EXPECT_THAT(output.child_counts, ::testing::ElementsAreArray(expected_child_counts));
  EXPECT_THAT(output.parent_indices, ::testing::ElementsAreArray(expected_parent_indices));
}

#undef CHECK_GLOBAL_TOPOLOGY_DATA

}  // namespace flatland::test
