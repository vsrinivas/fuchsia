// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/reachability.h"

#include <set>

#include <gtest/gtest.h>

#include "src/media/audio/services/mixer/fidl/testing/fake_graph.h"

namespace media_audio {

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

  for (NodeId src = 1; src <= 6; src++) {
    for (NodeId dest = 1; dest <= 6; dest++) {
      bool expect_path = (paths.count({src, dest}) > 0);
      EXPECT_EQ(ExistsPath(*graph.node(src), *graph.node(dest)), expect_path)
          << "src=" << src << ", dest=" << dest;
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

  for (NodeId src = 1; src <= 9; src++) {
    for (NodeId dest = 1; dest <= 9; dest++) {
      bool expect_path = (paths.count({src, dest}) > 0);
      EXPECT_EQ(ExistsPath(*graph.node(src), *graph.node(dest)), expect_path)
          << "src=" << src << ", dest=" << dest;
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
  for (auto src : nodes) {
    for (auto dest : nodes) {
      bool expect_path = (paths.count({src, dest}) > 0);
      EXPECT_EQ(ExistsPath(*graph.node(src), *graph.node(dest)), expect_path)
          << "src=" << src << ", dest=" << dest;
    }
  }
}

}  // namespace media_audio
