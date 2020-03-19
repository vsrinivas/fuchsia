// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/global_topology_data.h"

#include <memory>

#include <gtest/gtest.h>

#include "gmock/gmock.h"
#include "src/lib/fxl/logging.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtx/matrix_transform_2d.hpp>

using flatland::TransformGraph;
using flatland::TransformHandle;

namespace {

constexpr TransformHandle::InstanceId kLinkInstanceId = 0;

// Gets the test-standard link handle to link to a graph rooted at |instance_id:0|.
TransformHandle GetLinkHandle(uint64_t instance_id) { return {kLinkInstanceId, instance_id}; }

// Creates a link in |links| to the the graph rooted at |instance_id:0|.
void MakeLink(flatland::LinkSystem::LinkTopologyMap& links, uint64_t instance_id) {
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
  LinkSystem::LinkTopologyMap links;

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
  LinkSystem::LinkTopologyMap links;

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
  LinkSystem::LinkTopologyMap links;

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
  LinkSystem::LinkTopologyMap links;

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

TEST(GlobalTopologyDataTest, EmptyTopologyReturnsEmptyMatrices) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::TopologyVector topology_vector;
  GlobalTopologyData::ParentIndexVector parent_indices;

  auto output =
      GlobalTopologyData::ComputeGlobalMatrices(topology_vector, parent_indices, uber_structs);
  EXPECT_TRUE(output.empty());
}

TEST(GlobalTopologyDataTest, EmptyLocalMatricesAreIdentity) {
  UberStruct::InstanceMap uber_structs;

  // Make a global topology representing the following graph:
  //
  // 1:0 - 1:1
  GlobalTopologyData::TopologyVector topology_vector = {{1, 0}, {1, 1}};
  GlobalTopologyData::ParentIndexVector parent_indices = {0, 0};

  // The UberStruct for instance ID 1 must exist, but it contains no local matrices.
  auto uber_struct = std::make_unique<UberStruct>();
  uber_structs[1] = std::move(uber_struct);

  // The root matrix is set to the identity matrix, and the second inherits that.
  std::vector<glm::mat3> expected_matrices = {
      glm::mat3(),
      glm::mat3(),
  };

  auto output =
      GlobalTopologyData::ComputeGlobalMatrices(topology_vector, parent_indices, uber_structs);
  EXPECT_THAT(output, ::testing::ElementsAreArray(expected_matrices));
}

TEST(GlobalTopologyDataTest, GlobalMatricesIncludeParentMatrix) {
  UberStruct::InstanceMap uber_structs;

  // Make a global topology representing the following graph:
  //
  // 1:0 - 1:1 - 1:2
  //     \
  //       1:3 - 1:4
  GlobalTopologyData::TopologyVector topology_vector = {{1, 0}, {1, 1}, {1, 2}, {1, 3}, {1, 4}};
  GlobalTopologyData::ParentIndexVector parent_indices = {0, 0, 1, 0, 3};

  auto uber_struct = std::make_unique<UberStruct>();

  static const glm::vec2 kTranslation = {1.f, 2.f};
  static const float kRotation = glm::half_pi<float>();
  static const glm::vec2 kScale = {3.f, 5.f};

  // All transforms will get the translation from 1:0
  uber_struct->local_matrices[{1, 0}] = glm::translate(glm::mat3(), kTranslation);

  // The 1:1 - 1:2 branch rotates, then scales.
  uber_struct->local_matrices[{1, 1}] = glm::rotate(glm::mat3(), kRotation);
  uber_struct->local_matrices[{1, 2}] = glm::scale(glm::mat3(), kScale);

  // The 1:3 - 1:4 branch scales, then rotates.
  uber_struct->local_matrices[{1, 3}] = glm::scale(glm::mat3(), kScale);
  uber_struct->local_matrices[{1, 4}] = glm::rotate(glm::mat3(), kRotation);

  uber_structs[1] = std::move(uber_struct);

  // The expected matrices apply the operations in the correct order. The translation always comes
  // first, followed by the operations of the children.
  std::vector<glm::mat3> expected_matrices = {
      glm::translate(glm::mat3(), kTranslation),
      glm::rotate(glm::translate(glm::mat3(), kTranslation), kRotation),
      glm::scale(glm::rotate(glm::translate(glm::mat3(), kTranslation), kRotation), kScale),
      glm::scale(glm::translate(glm::mat3(), kTranslation), kScale),
      glm::rotate(glm::scale(glm::translate(glm::mat3(), kTranslation), kScale), kRotation),
  };

  auto output =
      GlobalTopologyData::ComputeGlobalMatrices(topology_vector, parent_indices, uber_structs);
  EXPECT_THAT(output, ::testing::ElementsAreArray(expected_matrices));
}

TEST(GlobalTopologyDataTest, GlobalMatricesMultipleUberStructs) {
  UberStruct::InstanceMap uber_structs;

  // Make a global topology representing the following graph:
  //
  // 1:0 - 2:0
  //     \
  //       1:1
  GlobalTopologyData::TopologyVector topology_vector = {{1, 0}, {2, 0}, {1, 1}};
  GlobalTopologyData::ParentIndexVector parent_indices = {0, 0, 0};

  auto uber_struct1 = std::make_unique<UberStruct>();
  auto uber_struct2 = std::make_unique<UberStruct>();

  // Each matrix scales by a different prime number to distinguish the branches.
  uber_struct1->local_matrices[{1, 0}] = glm::scale(glm::mat3(), {2.f, 2.f});
  uber_struct1->local_matrices[{1, 1}] = glm::scale(glm::mat3(), {3.f, 3.f});

  uber_struct2->local_matrices[{2, 0}] = glm::scale(glm::mat3(), {5.f, 5.f});

  uber_structs[1] = std::move(uber_struct1);
  uber_structs[2] = std::move(uber_struct2);

  std::vector<glm::mat3> expected_matrices = {
      glm::scale(glm::mat3(), glm::vec2(2.f)),   // 1:0 = 2
      glm::scale(glm::mat3(), glm::vec2(10.f)),  // 1:0 * 2:0 = 2 * 5 = 10
      glm::scale(glm::mat3(), glm::vec2(6.f)),   // 1:0 * 1:1 = 2 * 3 = 6
  };

  auto output =
      GlobalTopologyData::ComputeGlobalMatrices(topology_vector, parent_indices, uber_structs);
  EXPECT_THAT(output, ::testing::ElementsAreArray(expected_matrices));
}

#undef CHECK_GLOBAL_TOPOLOGY_DATA

}  // namespace test
}  // namespace flatland
