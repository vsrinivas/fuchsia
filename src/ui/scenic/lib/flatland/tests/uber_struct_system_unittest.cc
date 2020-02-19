// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/uber_struct_system.h"

#include <chrono>
#include <mutex>
#include <thread>

#include <gtest/gtest.h>

#include "gmock/gmock.h"
#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/flatland/global_topology_data.h"
#include "src/ui/scenic/lib/flatland/link_system.h"
#include "src/ui/scenic/lib/flatland/transform_handle.h"

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
// |data| is a GlobalTopologyData object. |link_id| is the instance ID for link handles.
#define CHECK_GLOBAL_TOPOLOGY_DATA(data, link_id)       \
  {                                                     \
    std::unordered_set<TransformHandle> all_handles;    \
    for (auto entry : data.topology_vector) {           \
      all_handles.insert(entry.handle);                 \
      EXPECT_NE(entry.handle.GetInstanceId(), link_id); \
    }                                                   \
    EXPECT_EQ(all_handles, data.live_handles);          \
  }

TEST(UberStructSystemTest, InstanceIdUniqueness) {
  UberStructSystem system;

  static constexpr uint64_t kNumThreads = 10;
  static constexpr uint64_t kNumInstanceIds = 100;
  static constexpr uint64_t kNumHandles = 10;

  std::mutex mutex;
  std::set<TransformHandle> handles;
  std::vector<std::thread> threads;

  const auto now = std::chrono::steady_clock::now();
  const auto then = now + std::chrono::milliseconds(50);

  for (uint64_t t = 0; t < kNumThreads; ++t) {
    std::thread thread([then, &system, &handles, &mutex]() {
      // Because each of the threads do a fixed amount of work, they may trigger in succession
      // without overlap. In order to bombard the system with concurrent instance ID requests, we
      // stall thread execution to a synchronized time.
      std::this_thread::sleep_until(then);
      std::vector<uint64_t> instance_ids;
      for (uint64_t i = 0; i < kNumInstanceIds; ++i) {
        // GetNextInstanceId() is the function that we're testing for concurrency.
        instance_ids.push_back(system.GetNextInstanceId());

        // Yield with some randomness so the threads get jumbled up a bit.
        if (std::rand() % 4 == 0) {
          std::this_thread::yield();
        }
      }

      // Acquire the test mutex and insert all handles into a set for later evaluation.
      {
        std::scoped_lock lock(mutex);
        for (const auto& id : instance_ids) {
          for (uint64_t h = 0; h < kNumHandles; ++h) {
            handles.insert({id, h});
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
  EXPECT_EQ(handles.size(), kNumThreads * kNumInstanceIds * kNumHandles);
}

TEST(UberStructSystemTest, BasicTopologyRetrieval) {
  UberStructSystem system;

  // This test consists of three isolated vectors. We confirm that we get back the appropriate
  // vector when we query for the root node of each topology.
  TransformGraph::TopologyVector vectors[] = {{{{0, 0}, 1}, {{0, 1}, 0}},   // 0:0 - 0:1
                                                                            //
                                              {{{1, 0}, 1}, {{1, 1}, 0}},   // 1:0 - 1:1
                                                                            //
                                              {{{2, 0}, 1}, {{2, 1}, 0}}};  // 2:0 - 2:1

  for (const auto& v : vectors) {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = v;
    system.SetUberStruct(v[0].handle.GetInstanceId(), std::move(uber_struct));
  }

  auto snapshot = system.Snapshot();
  for (const auto& v : vectors) {
    auto iter = snapshot.find(v[0].handle.GetInstanceId());
    EXPECT_NE(iter, snapshot.end());
    EXPECT_EQ(iter->second->local_topology, v);
  }
}

TEST(UberStructSystemTest, GlobalTopologyMultithreadedUpdates) {
  UberStructSystem system;

  auto link_2 = GetLinkHandle(2);
  auto link_3 = GetLinkHandle(3);
  auto link_4 = GetLinkHandle(4);
  auto link_5 = GetLinkHandle(5);
  auto link_6 = GetLinkHandle(6);
  auto link_7 = GetLinkHandle(7);
  auto link_8 = GetLinkHandle(8);
  auto link_9 = GetLinkHandle(9);
  auto link12 = GetLinkHandle(12);
  auto link13 = GetLinkHandle(13);

  // All of the non-leaf graphs have the same shape.
  //
  // X:0 - 0:2*X
  //     \
  //       0:2*X+1
  //
  // Where 0:Y is a link to the graph with root node Y:0. Because only graphs 1, 2, 3, 4, and 6 have
  // this shape, the tree is lopsided. The remaining graphs are all a single leaf node.
  //
  // 1 - 2 - 4 - 8
  //  \    \   \
  //   \     5   9
  //    \
  //     3 - 6 - 12
  //       \   \
  //         7   13
  TransformGraph::TopologyVector vectors[] = {
      {{{1, 0}, 2}, {link_2, 0}, {link_3, 0}},  // 1:0 - 0:2
                                                //     \
                                                //       0:3
                                                //
      {{{2, 0}, 2}, {link_4, 0}, {link_5, 0}},  // 2:0 - 0:4
                                                //     \
                                                //       0:5
                                                //
      {{{3, 0}, 2}, {link_6, 0}, {link_7, 0}},  // 3:0 - 0:6
                                                //     \
                                                //       0:7
                                                //
      {{{4, 0}, 2}, {link_8, 0}, {link_9, 0}},  // 3:0 - 0:8
                                                //     \
                                                //       0:9
                                                //
      {{{6, 0}, 2}, {link12, 0}, {link13, 0}},  // 6:0 - 0:12
                                                //     \
                                                //       0:13
                                                //
      {{{5, 0}, 0}},                            // 5:0
                                                //
      {{{7, 0}, 0}},                            // 7:0
                                                //
      {{{8, 0}, 0}},                            // 8:0
                                                //
      {{{9, 0}, 0}},                            // 9:0
                                                //
      {{{12, 0}, 0}},                           // 12:0
                                                //
      {{{13, 0}, 0}},                           // 13:0
  };

  // These graphs swap nodes that are an equivalent shape from the original graph.
  //
  // 1 - 3 - 4 - 13
  //  \    \   \
  //   \     5   12
  //    \
  //     2 - 6 - 9
  //       \   \
  //         7   8
  TransformGraph::TopologyVector alternate_vectors[] = {
      {{{1, 0}, 2}, {link_3, 0}, {{0, 2}, 0}},  // 1:0 - 0:3
                                                //     \
                                                //       0:2
                                                //
      {{{2, 0}, 2}, {link_6, 0}, {link_7, 0}},  // 2:0 - 0:6
                                                //     \
                                                //       0:7
                                                //
      {{{3, 0}, 2}, {link_4, 0}, {link_5, 0}},  // 3:0 - 0:4
                                                //     \
                                                //       0:5
                                                //
      {{{4, 0}, 2}, {link13, 0}, {link12, 0}},  // 3:0 - 0:13
                                                //     \
                                                //       0:12
                                                //
      {{{6, 0}, 2}, {link_9, 0}, {link_8, 0}},  // 6:0 - 0:9
                                                //     \
                                                //       0:8
                                                //
      {{{5, 0}, 0}},                            // 5:0
                                                //
      {{{7, 0}, 0}},                            // 7:0
                                                //
      {{{8, 0}, 0}},                            // 8:0
                                                //
      {{{9, 0}, 0}},                            // 9:0
                                                //
      {{{12, 0}, 0}},                           // 12:0
                                                //
      {{{13, 0}, 0}},                           // 13:0
  };

  // Every relevant 0:X node should link to X:0.
  LinkSystem::LinkTopologyMap links;
  for (uint64_t i = 2; i <= 13; ++i) {
    MakeLink(links, i);
  }

  // Initialize the graph.
  for (const auto& v : vectors) {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = v;
    system.SetUberStruct(v[0].handle.GetInstanceId(), std::move(uber_struct));
  }

  // The expected output child counts should be the same regardless.
  const std::vector<uint64_t> expected_child_counts = {2, 2, 2, 0, 0, 0, 2, 2, 0, 0, 0};

  std::vector<std::thread> threads;
  bool run = true;

  // Only swap out the first 5 vectors, since the remaining are just leaf graphs.
  for (uint64_t t = 0; t < 5; ++t) {
    std::thread thread([&system, &run, v = vectors[t], a = alternate_vectors[t]]() {
      while (run) {
        {
          auto uber_struct = std::make_unique<UberStruct>();
          uber_struct->local_topology = v;
          system.SetUberStruct(v[0].handle.GetInstanceId(), std::move(uber_struct));
        }

        {
          auto uber_struct = std::make_unique<UberStruct>();
          uber_struct->local_topology = a;
          system.SetUberStruct(a[0].handle.GetInstanceId(), std::move(uber_struct));
        }
      }
    });

    threads.push_back(std::move(thread));
  }

  static constexpr uint64_t kNumChecks = 100;

  for (uint64_t i = 0; i < kNumChecks; ++i) {
    // Because the threads always swap out each graph with an equivalent alternate graph, any
    // intermediate state, with a mix of graphs, should always produce the same set of parent
    // indexes.
    auto output = GlobalTopologyData::ComputeGlobalTopologyData(system.Snapshot(), links,
                                                                kLinkInstanceId, {1, 0});
    CHECK_GLOBAL_TOPOLOGY_DATA(output, 0u);

    std::vector<uint64_t> child_counts;
    child_counts.resize(output.topology_vector.size());
    for (uint64_t i = 0; i < output.topology_vector.size(); i++) {
      child_counts[i] = output.topology_vector[i].child_count;
    }
    EXPECT_THAT(child_counts, ::testing::ElementsAreArray(expected_child_counts));

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
