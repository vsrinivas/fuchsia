// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/global_topology_data.h"

#include <gtest/gtest.h>

#include "gmock/gmock.h"
#include "src/lib/fxl/logging.h"

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
#define CHECK_GLOBAL_TOPOLOGY_DATA(data, link_id)       \
  {                                                     \
    std::unordered_set<TransformHandle> all_handles;    \
    for (auto entry : data.topology_vector) {           \
      all_handles.insert(entry.handle);                 \
      EXPECT_NE(entry.handle.GetInstanceId(), link_id); \
    }                                                   \
    EXPECT_EQ(all_handles, data.live_handles);          \
  }

TEST(GlobalTopologyDataTest, GlobalTopologyUnknownGraph) {
  TransformHandle unknown_handle = {1, 1};

  auto output =
      GlobalTopologyData::ComputeGlobalTopologyData({}, {}, kLinkInstanceId, unknown_handle);
  EXPECT_TRUE(output.topology_vector.empty());
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
  TransformGraph::TopologyVector expected_topology = {{{1, 0}, 1}, {{2, 0}, 0}};
  auto output =
      GlobalTopologyData::ComputeGlobalTopologyData(uber_structs, links, kLinkInstanceId, {1, 0});
  CHECK_GLOBAL_TOPOLOGY_DATA(output, 0u);
  EXPECT_THAT(output.topology_vector, ::testing::ElementsAreArray(expected_topology));
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
  TransformGraph::TopologyVector expected_topology = {{{1, 0}, 2}, {{1, 1}, 0}, {{1, 2}, 0}};

  auto uber_struct = std::make_unique<UberStruct>();
  uber_struct->local_topology = vectors[0];
  uber_structs[vectors[0][0].handle.GetInstanceId()] = std::move(uber_struct);

  auto output =
      GlobalTopologyData::ComputeGlobalTopologyData(uber_structs, links, kLinkInstanceId, {1, 0});
  CHECK_GLOBAL_TOPOLOGY_DATA(output, 0u);
  EXPECT_THAT(output.topology_vector, ::testing::ElementsAreArray(expected_topology));

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

  // When the link becomes available, the full topology is available, excluding the link handle.
  //
  // 1:0 - 1:1
  //   \ \
  //    \  2:0 - 2:1
  //     \
  //       1:2
  expected_topology = {{{1, 0}, 3}, {{1, 1}, 0}, {{2, 0}, 1}, {{2, 1}, 0}, {{1, 2}, 0}};

  MakeLink(links, 2);  // 0:2 - 2:0

  output =
      GlobalTopologyData::ComputeGlobalTopologyData(uber_structs, links, kLinkInstanceId, {1, 0});
  CHECK_GLOBAL_TOPOLOGY_DATA(output, 0u);
  EXPECT_THAT(output.topology_vector, ::testing::ElementsAreArray(expected_topology));
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

  TransformGraph::TopologyVector expected_topology = {{{1, 0}, 0}};

  auto output =
      GlobalTopologyData::ComputeGlobalTopologyData(uber_structs, links, kLinkInstanceId, {1, 0});
  CHECK_GLOBAL_TOPOLOGY_DATA(output, 0u);
  EXPECT_THAT(output.topology_vector, ::testing::ElementsAreArray(expected_topology));

  // Changing the link to the right root handle of 2:0 completes the topology.
  MakeLink(links, 2);  // 0:2 - 2:0

  // So the expected topology, excluding the link handle:
  //
  // 1:0 - 2:0
  expected_topology = {{{1, 0}, 1}, {{2, 0}, 0}};

  output =
      GlobalTopologyData::ComputeGlobalTopologyData(uber_structs, links, kLinkInstanceId, {1, 0});
  CHECK_GLOBAL_TOPOLOGY_DATA(output, 0u);
  EXPECT_THAT(output.topology_vector, ::testing::ElementsAreArray(expected_topology));
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
  TransformGraph::TopologyVector expected_topology = {
      {{1, 0}, 2}, {{2, 0}, 2}, {{2, 1}, 0}, {{3, 0}, 0}, {{3, 0}, 0}};
  auto output =
      GlobalTopologyData::ComputeGlobalTopologyData(uber_structs, links, kLinkInstanceId, {1, 0});
  CHECK_GLOBAL_TOPOLOGY_DATA(output, 0u);
  EXPECT_THAT(output.topology_vector, ::testing::ElementsAreArray(expected_topology));
}

#undef CHECK_GLOBAL_TOPOLOGY_DATA

}  // namespace test
}  // namespace flatland
