// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/reachability.h"

#include <set>

#include <gtest/gtest.h>

#include "src/media/audio/services/mixer/fidl/testing/fake_graph.h"

namespace media_audio {

TEST(ReachabilityTest, ComputeDelayOrdinaryNodesOnly) {
  // Node graph is structured as follows:
  //
  // ```
  //   1     2
  //   |     |
  //   |     3
  //    \   /
  //      |
  //      4
  //      |
  //      5
  // ```
  FakeGraph graph({
      .edges =
          {
              {1, 4},
              {2, 3},
              {3, 4},
              {4, 5},
          },
  });

  // Total delays should be all zero by default, since there are no self delay contributions yet.
  SCOPED_TRACE("no delay");
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(1), nullptr), zx::nsec(0));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(2), nullptr), zx::nsec(0));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(3), graph.node(2)), zx::nsec(0));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(4), graph.node(1)), zx::nsec(0));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(4), graph.node(3)), zx::nsec(0));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(5), graph.node(4)), zx::nsec(0));

  EXPECT_EQ(ComputeUpstreamDelay(graph.node(1)), zx::nsec(0));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(2)), zx::nsec(0));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(3)), zx::nsec(0));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(4)), zx::nsec(0));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(5)), zx::nsec(0));

  // Set delay of 1nsec for node `1`.
  SCOPED_TRACE("add delay to node 1");
  graph.node(1)->SetOnGetSelfPresentationDelayForSource([](const NodePtr& source) {
    FX_CHECK(source == nullptr);
    return zx::nsec(1);
  });

  EXPECT_EQ(ComputeDownstreamDelay(graph.node(1), nullptr), zx::nsec(1));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(2), nullptr), zx::nsec(0));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(3), graph.node(2)), zx::nsec(0));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(4), graph.node(1)), zx::nsec(0));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(4), graph.node(3)), zx::nsec(0));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(5), graph.node(4)), zx::nsec(0));

  EXPECT_EQ(ComputeUpstreamDelay(graph.node(1)), zx::nsec(1));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(2)), zx::nsec(0));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(3)), zx::nsec(0));
  // Upstream delay onwards should pick the maximum of source nodes `1` and `3`.
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(4)), zx::nsec(1));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(5)), zx::nsec(1));

  // Set delay of 5nsec for node `5`.
  SCOPED_TRACE("add delay to node 5");
  graph.node(5)->SetOnGetSelfPresentationDelayForSource([&](const NodePtr& source) {
    FX_CHECK(source == graph.node(4));
    return zx::nsec(5);
  });

  EXPECT_EQ(ComputeDownstreamDelay(graph.node(1), nullptr), zx::nsec(6));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(2), nullptr), zx::nsec(5));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(3), graph.node(2)), zx::nsec(5));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(4), graph.node(1)), zx::nsec(5));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(4), graph.node(3)), zx::nsec(5));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(5), graph.node(4)), zx::nsec(5));

  EXPECT_EQ(ComputeUpstreamDelay(graph.node(1)), zx::nsec(1));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(2)), zx::nsec(0));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(3)), zx::nsec(0));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(4)), zx::nsec(1));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(5)), zx::nsec(6));

  // Set variable delay of 1nsec or 3nsec for node `4`, depending on source nodes `1` or `3`.
  SCOPED_TRACE("add delay to node 4");
  graph.node(4)->SetOnGetSelfPresentationDelayForSource([&](const NodePtr& source) {
    FX_CHECK(source == graph.node(1) || source == graph.node(3));
    return (source == graph.node(1)) ? zx::nsec(1) : zx::nsec(3);
  });

  // Downstream delay picks node `4` delay for source node `1`, which totals to `5 + 1 + 1 = 7`.
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(1), nullptr), zx::nsec(7));
  // Downstream delay picks node `4` delay for source node `3`, which totals to `5 + 3 + 0 + 0 = 8`.
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(2), nullptr), zx::nsec(8));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(3), graph.node(2)), zx::nsec(8));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(4), graph.node(1)), zx::nsec(6));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(4), graph.node(3)), zx::nsec(8));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(5), graph.node(4)), zx::nsec(5));

  EXPECT_EQ(ComputeUpstreamDelay(graph.node(1)), zx::nsec(1));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(2)), zx::nsec(0));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(3)), zx::nsec(0));
  // Upstream delay onwards picks the maximum of the total delay between source nodes `1` and `3`,
  // which is `1 + 1 = 2` vs `0 + 3 = 3`.
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(4)), zx::nsec(3));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(5)), zx::nsec(8));
}

TEST(ReachabilityTest, ComputeDelayWithMetaNodes) {
  FakeGraph graph({
      .meta_nodes =
          {
              {
                  10,
                  {.source_children = {}, .dest_children = {1, 2}},
              },
              {
                  20,
                  {.source_children = {3}, .dest_children = {4}},
              },
              {
                  30,
                  {.source_children = {5}, .dest_children = {}},
              },
          },
      .edges =
          {
              {1, 3},
              {2, 5},
              {4, 5},
          },
  });

  // Total delays should be all zero by default, since there are no self delay contributions yet.
  SCOPED_TRACE("no delay");
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(1), nullptr), zx::nsec(0));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(2), nullptr), zx::nsec(0));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(3), graph.node(1)), zx::nsec(0));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(4), nullptr), zx::nsec(0));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(5), graph.node(2)), zx::nsec(0));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(5), graph.node(4)), zx::nsec(0));

  EXPECT_EQ(ComputeUpstreamDelay(graph.node(1)), zx::nsec(0));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(2)), zx::nsec(0));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(3)), zx::nsec(0));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(4)), zx::nsec(0));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(5)), zx::nsec(0));

  // Set delay of 1nsec for destination child node `1` of meta node `10`.
  SCOPED_TRACE("add delay to destination child node 1");
  graph.node(1)->SetOnGetSelfPresentationDelayForSource([](const NodePtr& source) {
    FX_CHECK(source == nullptr);
    return zx::nsec(1);
  });

  EXPECT_EQ(ComputeDownstreamDelay(graph.node(1), nullptr), zx::nsec(1));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(2), nullptr), zx::nsec(0));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(3), graph.node(1)), zx::nsec(0));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(4), nullptr), zx::nsec(0));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(5), graph.node(2)), zx::nsec(0));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(5), graph.node(4)), zx::nsec(0));

  EXPECT_EQ(ComputeUpstreamDelay(graph.node(1)), zx::nsec(1));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(2)), zx::nsec(0));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(3)), zx::nsec(1));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(4)), zx::nsec(1));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(5)), zx::nsec(1));

  // Set variable delay of 4nsec or 2nsec for source child node `5` of meta node `30` depending on
  // the connected destination child nodes `2` and `4`.
  SCOPED_TRACE("add delay to source child node 5");
  graph.node(5)->SetOnGetSelfPresentationDelayForSource([&](const NodePtr& source) {
    FX_CHECK(source == graph.node(2) || source == graph.node(4));
    return (source == graph.node(2)) ? zx::nsec(2) : zx::nsec(4);
  });

  // Downstream delay should pick maximum of destination child nodes `2` and `4`, which will sum up
  // to `4 + 0 + 0 + 1 = 5`.
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(1), nullptr), zx::nsec(5));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(2), nullptr), zx::nsec(2));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(3), graph.node(1)), zx::nsec(4));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(4), nullptr), zx::nsec(4));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(5), graph.node(2)), zx::nsec(2));
  EXPECT_EQ(ComputeDownstreamDelay(graph.node(5), graph.node(4)), zx::nsec(4));

  EXPECT_EQ(ComputeUpstreamDelay(graph.node(1)), zx::nsec(1));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(2)), zx::nsec(0));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(3)), zx::nsec(1));
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(4)), zx::nsec(1));
  // Upstream delay should pick maximum of destination child nodes `2` and `4`, which will sum up to
  // `1 + 0 + 0 + 4 = 5`.
  EXPECT_EQ(ComputeUpstreamDelay(graph.node(5)), zx::nsec(5));
}

TEST(ReachabilityTest, OrdinaryNodeSelfEdge) {
  FakeGraph graph({
      .edges = {{1, 1}},
  });

  // Self edge 1<->1.
  EXPECT_TRUE(ExistsPath(*graph.node(1), *graph.node(1)));
}

TEST(ReachabilityTest, OrdinaryNodes) {
  // With ordinary nodes only, the graph must be an inverted tree.
  FakeGraph graph({
      .edges =
          {
              {1, 3},  //
              {2, 3},
              {3, 4},
              {5, 4},
              {4, 6},
              {6, 5},  // cycle
          },
  });

  // Paths that exist. Note cycle from 5->4.
  std::set<std::pair<NodeId, NodeId>> paths{
      {1, 3}, {1, 4}, {1, 5}, {1, 6},  //
      {2, 3}, {2, 4}, {2, 5}, {2, 6},  //
      {3, 4}, {3, 5}, {3, 6},          //
      {4, 4}, {4, 5}, {4, 6},          //
      {5, 4}, {5, 5}, {5, 6},          //
      {6, 4}, {6, 5}, {6, 6},          //
  };

  for (NodeId source = 1; source <= 6; source++) {
    for (NodeId dest = 1; dest <= 6; dest++) {
      bool expect_path = (paths.count({source, dest}) > 0);
      EXPECT_EQ(ExistsPath(*graph.node(source), *graph.node(dest)), expect_path)
          << "source=" << source << ", dest=" << dest;
    }
  }
}

TEST(ReachabilityTest, MetaNodeSelfEdge) {
  FakeGraph graph({
      .meta_nodes =
          {
              {
                  1,
                  {.source_children = {2}, .dest_children = {3}},
              },
          },
      .edges = {{3, 2}},
  });

  // Self edge 1<->1.
  EXPECT_TRUE(ExistsPath(*graph.node(1), *graph.node(1)));
  EXPECT_TRUE(ExistsPath(*graph.node(2), *graph.node(2)));
  EXPECT_TRUE(ExistsPath(*graph.node(3), *graph.node(3)));
}

TEST(ReachabilityTest, MetaNodes) {
  FakeGraph graph({
      .meta_nodes =
          {
              {
                  3,
                  {.source_children = {1, 2}, .dest_children = {4, 5}},
              },
              {
                  8,
                  {.source_children = {6, 7}, .dest_children = {9}},
              },
          },
      .edges = {{5, 7}},
  });

  // Paths that exist.
  std::set<std::pair<NodeId, NodeId>> paths{
      {1, 3}, {1, 4}, {1, 5}, {1, 7}, {1, 8}, {1, 9},  //
      {2, 3}, {2, 4}, {2, 5}, {2, 7}, {2, 8}, {2, 9},  //
      {3, 4}, {3, 5}, {3, 7}, {3, 8}, {3, 9},          //
      {5, 7}, {5, 8}, {5, 9},                          //
      {6, 8}, {6, 9},                                  //
      {7, 8}, {7, 9},                                  //
      {8, 9},                                          //
  };

  for (NodeId source = 1; source <= 9; source++) {
    for (NodeId dest = 1; dest <= 9; dest++) {
      bool expect_path = (paths.count({source, dest}) > 0);
      EXPECT_EQ(ExistsPath(*graph.node(source), *graph.node(dest)), expect_path)
          << "source=" << source << ", dest=" << dest;
    }
  }
}

TEST(ReachabilityTest, MetaAndOrdinaryNodes) {
  FakeGraph graph({
      .meta_nodes =
          {
              {
                  23,
                  {.source_children = {21, 22}, .dest_children = {24, 25}},
              },
              {
                  63,
                  {.source_children = {61, 62}, .dest_children = {64, 65}},
              },
          },
      .edges =
          {
              {10, 21},
              {24, 30},
              {25, 40},
              {40, 50},
              {50, 62},
              {65, 70},
          },
  });

  // Paths that exist.
  std::set<std::pair<NodeId, NodeId>> paths{
      {10, 21}, {10, 23}, {10, 24}, {10, 30}, {10, 25}, {10, 40}, {10, 50},
      {10, 62}, {10, 63}, {10, 64}, {10, 65}, {10, 70},

      {21, 23}, {21, 24}, {21, 30}, {21, 25}, {21, 40}, {21, 50}, {21, 62},
      {21, 63}, {21, 64}, {21, 65}, {21, 70},

      {22, 23}, {22, 24}, {22, 30}, {22, 25}, {22, 40}, {22, 50}, {22, 62},
      {22, 63}, {22, 64}, {22, 65}, {22, 70},

      {23, 24}, {23, 30}, {23, 25}, {23, 40}, {23, 50}, {23, 62}, {23, 63},
      {23, 64}, {23, 65}, {23, 70},

      {24, 30},

      {25, 40}, {25, 50}, {25, 62}, {25, 63}, {25, 64}, {25, 65}, {25, 70},

      {40, 50}, {40, 62}, {40, 63}, {40, 64}, {40, 65}, {40, 70},

      {50, 62}, {50, 63}, {50, 64}, {50, 65}, {50, 70},

      {61, 63}, {61, 64}, {61, 65}, {61, 70},

      {62, 63}, {62, 64}, {62, 65}, {62, 70},

      {63, 64}, {63, 65}, {63, 70},

      {65, 70},
  };

  std::vector<NodeId> nodes{10, 21, 22, 23, 24, 25, 30, 40, 50, 61, 62, 63, 64, 64, 70};
  for (auto source : nodes) {
    for (auto dest : nodes) {
      bool expect_path = (paths.count({source, dest}) > 0);
      EXPECT_EQ(ExistsPath(*graph.node(source), *graph.node(dest)), expect_path)
          << "source=" << source << ", dest=" << dest;
    }
  }
}

}  // namespace media_audio
