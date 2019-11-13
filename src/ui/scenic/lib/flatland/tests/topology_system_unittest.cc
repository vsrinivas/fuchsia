// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/topology_system.h"

#include <chrono>
#include <mutex>
#include <thread>

#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"

namespace flatland {
namespace test {

TEST(TopologySystemTest, GraphUniqueness) {
  TopologySystem system;

  static constexpr uint64_t kNumThreads = 10;
  static constexpr uint64_t kNumGraphs = 100;
  static constexpr uint64_t kNumHandles = 10;

  std::mutex mutex;
  std::set<TransformHandle> handles;
  std::vector<std::thread> threads;

  const auto now = std::chrono::steady_clock::now();
  const auto then = now + std::chrono::milliseconds(50);

  for (uint64_t t = 0; t < kNumThreads; ++t) {
    std::thread thread([then, &system, &handles, &mutex]() {
      // Because each of the threads do a fixed amount of work, they may trigger in succession
      // without overlap. In order to bombard the system with concurrent graph creation requests, we
      // stall thread execution to a synchronized time.
      std::this_thread::sleep_until(then);
      std::vector<TransformGraph> graphs;
      for (uint64_t g = 0; g < kNumGraphs; ++g) {
        // CreateGraph() is the function that we're testing for concurrency.
        graphs.push_back(system.CreateGraph());

        // Yield with some randomness so the threads get jumbled up a bit.
        if (std::rand() % 4 == 0) {
          std::this_thread::yield();
        }
      }

      // Acquire the test mutex and insert all handles into a set for later evaluation.
      {
        std::scoped_lock lock(mutex);
        for (auto& g : graphs) {
          for (uint64_t h = 0; h < kNumHandles; ++h) {
            handles.insert(g.CreateTransform());
          }
        }
      }
    });

    threads.push_back(std::move(thread));
  }

  for (auto& t : threads) {
    t.join();
  }

  // If all the handles are unique, the set's size should be equal to the number of handles
  // created.
  EXPECT_EQ(handles.size(), kNumThreads * kNumGraphs * kNumHandles);
}

TEST(TopologySystemTest, BasicRetrieval) {
  TopologySystem system;

  // This test consists of three isolated vectors. We confirm that we get back the appropriate
  // vector when we query for the root node of each topology.
  TransformGraph::TopologyVector vectors[] = {{{{0, 0}, 0}, {{0, 1}, 0}},   // 0:0 - 0:1
                                                                            //
                                              {{{1, 0}, 0}, {{1, 1}, 0}},   // 1:0 - 1:1
                                                                            //
                                              {{{2, 0}, 0}, {{2, 1}, 0}}};  // 2:0 - 2:1

  for (auto v : vectors) {
    system.SetLocalTopology(v);
  }

  for (auto v : vectors) {
    auto output = system.ComputeGlobalTopologyVector(v[0].handle);
    EXPECT_EQ(output, v);
  }
}

TEST(TopologySystemTest, BasicExpansion) {
  TopologySystem system;

  // This test consists of two vectors from the same graph_id. We confirm that the graph is
  // expanded, even if the graph_ids match.
  TransformGraph::TopologyVector vectors[] = {{{{0, 0}, 0}, {{0, 1}, 0}},   // 0:0 - 0:1
                                              {{{0, 1}, 0}, {{0, 2}, 0}}};  // 0:1 - 0:2

  for (auto v : vectors) {
    system.SetLocalTopology(v);
  }

  // Combined, the global vector looks like this.
  //
  // 0:0 - 0:1 - 0:2
  TransformGraph::TopologyVector expected_output = {{{0, 0}, 0}, {{0, 1}, 0}, {{0, 2}, 1}};
  auto output = system.ComputeGlobalTopologyVector({0, 0});
  EXPECT_EQ(expected_output, output);
}

TEST(TopologySystemTest, IndexFixup) {
  TopologySystem system;

  TransformGraph::TopologyVector vectors[] = {
      {{{0, 0}, 0}, {{1, 0}, 0}, {{2, 0}, 0}},   // 0:0 - 1:0
                                                 //     \
                                                 //       2:0
                                                 //
      {{{1, 0}, 0}, {{1, 1}, 0}, {{1, 2}, 0}},   // 1:0 - 1:1
                                                 //     \
                                                 //       1:2
                                                 //
      {{{2, 0}, 0}, {{2, 1}, 0}, {{2, 2}, 1}}};  // 2:0 - 2:1 - 2:2

  for (auto v : vectors) {
    system.SetLocalTopology(v);
  }

  // Combined, the global vector looks like this.
  //
  // 0:0 - 1:0 - 1:1
  //     \     \
  //      \      1:2
  //       \
  //       2:0 - 2:1 - 2:2
  TransformGraph::TopologyVector expected_output = {
      {{0, 0}, 0}, {{1, 0}, 0}, {{1, 1}, 1}, {{1, 2}, 1}, {{2, 0}, 0}, {{2, 1}, 4}, {{2, 2}, 5}};
  auto output = system.ComputeGlobalTopologyVector({0, 0});
  EXPECT_EQ(expected_output, output);

  // Replace graph 0 with a new vector which swaps the order of the children.
  //
  // 0:0 - 2:0
  //     \
  //       1:0
  TransformGraph::TopologyVector vector0_alternate = {{{0, 0}, 0}, {{2, 0}, 0}, {{1, 0}, 0}};
  system.SetLocalTopology(vector0_alternate);

  // Now, the new global vector should look like this.
  //
  // 0:0 - 2:0 - 2:1 - 2:2
  //      \
  //       1:0 - 1:1
  //            \
  //             1:2
  TransformGraph::TopologyVector expected_output_alternate = {
      {{0, 0}, 0}, {{2, 0}, 0}, {{2, 1}, 1}, {{2, 2}, 2}, {{1, 0}, 0}, {{1, 1}, 4}, {{1, 2}, 4}};
  output = system.ComputeGlobalTopologyVector({0, 0});
  EXPECT_EQ(expected_output_alternate, output);
}

TEST(TopologySystemTest, DanglingChild) {
  TopologySystem system;

  TransformGraph::TopologyVector vectors[] = {
      {{{0, 0}, 0}, {{1, 0}, 0}, {{2, 0}, 0}},   // 0:0 - 1:0
                                                 //     \
                                                 //       2:0
                                                 //
      {{{1, 0}, 0}, {{1, 1}, 0}, {{1, 2}, 0}},   // 1:0 - 1:1
                                                 //     \
                                                 //       1:2
                                                 //
      {{{2, 0}, 0}, {{2, 1}, 0}, {{2, 2}, 1}}};  // 2:0 - 2:1 - 2:2

  // With only the top level vector updated, we get the same result at the retrieval test.
  system.SetLocalTopology(vectors[0]);
  auto output = system.ComputeGlobalTopologyVector({0, 0});
  EXPECT_EQ(output, vectors[0]);

  // With the first and third vectors updated, we get a partial global listing. The
  // middle transform references a vector that has not been updated yet and, therefore, does not
  // expand beyond the referencing transform.
  //
  // 0:0 - 1:0
  //      \
  //       2:0 - 2:1 - 2:2
  TransformGraph::TopologyVector expected_output_partial = {
      {{0, 0}, 0}, {{1, 0}, 0}, {{2, 0}, 0}, {{2, 1}, 2}, {{2, 2}, 3}};
  system.SetLocalTopology(vectors[2]);
  output = system.ComputeGlobalTopologyVector({0, 0});
  EXPECT_EQ(expected_output_partial, output);

  // Combined, the global vector looks like this.
  //
  // 0:0 - 1:0 - 1:1
  //     \     \
  //      \      1:2
  //       \
  //       2:0 - 2:1 - 2:2
  TransformGraph::TopologyVector expected_output = {
      {{0, 0}, 0}, {{1, 0}, 0}, {{1, 1}, 1}, {{1, 2}, 1}, {{2, 0}, 0}, {{2, 1}, 4}, {{2, 2}, 5}};
  system.SetLocalTopology(vectors[1]);
  output = system.ComputeGlobalTopologyVector({0, 0});
  EXPECT_EQ(expected_output, output);
}

TEST(TopologySystemTest, DiamondInheritance) {
  TopologySystem system;

  TransformGraph::TopologyVector vectors[] = {
      {{{0, 0}, 0}, {{1, 0}, 0}, {{2, 0}, 0}},   // 0:0 - 1:0
                                                 //     \
                                                 //       2:0
                                                 //
      {{{1, 0}, 0}, {{1, 1}, 0}, {{3, 0}, 0}},   // 1:0 - 1:1
                                                 //     \
                                                 //       3:0
                                                 //
      {{{2, 0}, 0}, {{3, 0}, 0}, {{2, 2}, 1}},   // 2:0 - 3:0 - 2:2
                                                 //
      {{{3, 0}, 0}, {{3, 1}, 0}, {{3, 2}, 1}}};  // 3:0 - 3:1 - 3:2

  for (auto v : vectors) {
    system.SetLocalTopology(v);
  }

  // When fully combined, we expect to find two copies of the fourth subgraph.
  //
  // In addition, the rules for the connection from 3:0 to 2:2 in the third subgraph are subtle.
  // 2:2 should be a child of 3:0, but it should be the last child, after all other children of
  // 3:0 have been added to the system.
  //
  // 0:0 - 1:0 - 1:1
  //    \      \
  //     \       3:0 - 3:1 - 3:2
  //      \
  //       2:0 - 3:0 - 3:1 - 3:2
  //                  \
  //                   2:2
  TransformGraph::TopologyVector expected_output = {
      {{0, 0}, 0}, {{1, 0}, 0}, {{1, 1}, 1}, {{3, 0}, 1}, {{3, 1}, 3}, {{3, 2}, 4},
      {{2, 0}, 0}, {{3, 0}, 6}, {{3, 1}, 7}, {{3, 2}, 8}, {{2, 2}, 7}};
  auto output = system.ComputeGlobalTopologyVector({0, 0});
  EXPECT_EQ(expected_output, output);
}

TEST(TopologySystemTest, MultithreadedUpdates) {
  TopologySystem system;

  // All of these graphs have the same shape.
  //
  // X:0 - 2*X+1:0
  //     \
  //       2*X+2:0
  //
  // Because we only have graphs for X = 0,1,2,3, and 5, we end up with a lopsided graph.
  //
  // 0 - 1 - 3 - 7
  //  \    \   \
  //   \     4   8
  //    \
  //     2 - 5 - 11
  //       \   \
  //         6   12
  TransformGraph::TopologyVector vectors[] = {
      {{{0, 0}, 0}, {{1, 0}, 0}, {{2, 0}, 0}},    // 0:0 - 1:0
                                                  //     \
                                                  //       2:0
                                                  //
      {{{1, 0}, 0}, {{3, 0}, 0}, {{4, 0}, 0}},    // 1:0 - 3:0
                                                  //     \
                                                  //       4:0
                                                  //
      {{{2, 0}, 0}, {{5, 0}, 0}, {{6, 0}, 0}},    // 2:0 - 5:0
                                                  //     \
                                                  //       6:0
                                                  //
      {{{3, 0}, 0}, {{7, 0}, 0}, {{8, 0}, 0}},    // 3:0 - 7:0
                                                  //     \
                                                  //       8:0
                                                  //
      {{{5, 0}, 0}, {{11, 0}, 0}, {{12, 0}, 0}},  // 5:0 - 11:0
                                                  //     \
                                                  //       12:0
  };

  // These graphs swap nodes that are an equivalent shape from the original graph.
  //
  // 0 - 2 - 3 - 12
  //  \    \   \
  //   \     4   11
  //    \
  //     1 - 5 - 8
  //       \   \
  //         6   7
  TransformGraph::TopologyVector alternate_vectors[] = {
      {{{0, 0}, 0}, {{2, 0}, 0}, {{1, 0}, 0}},    // 0:0 - 2:0
                                                  //     \
                                                  //       1:0
                                                  //
      {{{1, 0}, 0}, {{5, 0}, 0}, {{6, 0}, 0}},    // 1:0 - 5:0
                                                  //     \
                                                  //       6:0
                                                  //
      {{{2, 0}, 0}, {{3, 0}, 0}, {{4, 0}, 0}},    // 2:0 - 3:0
                                                  //     \
                                                  //       4:0
                                                  //
      {{{3, 0}, 0}, {{12, 0}, 0}, {{11, 0}, 0}},  // 3:0 - 12:0
                                                  //     \
                                                  //       11:0
                                                  //
      {{{5, 0}, 0}, {{8, 0}, 0}, {{7, 0}, 0}},    // 5:0 - 8:0
                                                  //     \
                                                  //       7:0
  };

  // Initialize the graph.
  for (auto v : vectors) {
    system.SetLocalTopology(v);
  }

  std::vector<uint64_t> expected_indices = {0, 0, 1, 2, 2, 1, 0, 6, 7, 7, 6};
  auto output = system.ComputeGlobalTopologyVector({0, 0});
  EXPECT_EQ(output.size(), expected_indices.size());
  for (uint64_t i = 0; i < output.size(); i++) {
    EXPECT_EQ(output[i].parent_index, expected_indices[i]);
  }

  // Initialize the graph with alternate vectors.
  for (auto v : alternate_vectors) {
    system.SetLocalTopology(v);
  }

  output = system.ComputeGlobalTopologyVector({0, 0});
  EXPECT_EQ(output.size(), expected_indices.size());
  for (uint64_t i = 0; i < output.size(); i++) {
    EXPECT_EQ(output[i].parent_index, expected_indices[i]);
  }

  std::vector<std::thread> threads;
  bool run = true;

  for (uint64_t t = 0; t < 5; ++t) {
    std::thread thread([&system, &run, v = vectors[t], a = alternate_vectors[t]]() {
      while (run) {
        system.SetLocalTopology(v);
        system.SetLocalTopology(a);
      }
    });

    threads.push_back(std::move(thread));
  }

  static constexpr uint64_t kNumChecks = 100;

  for (uint64_t i = 0; i < kNumChecks; ++i) {
    // Because the threads always swap out each graph with an equivalent alternate graph, any
    // intermediate state, with a mix of graphs, should always produce the same set of parent
    // indexes.
    output = system.ComputeGlobalTopologyVector({0, 0});
    EXPECT_EQ(output.size(), expected_indices.size());
    for (uint64_t i = 0; i < output.size(); i++) {
      EXPECT_EQ(output[i].parent_index, expected_indices[i]);
    }

    // This sleep triggers the Compute call at a random point in the middle of all of the
    // thread updates.
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  run = false;
  for (auto& t : threads) {
    t.join();
  }
}

}  // namespace test
}  // namespace flatland
