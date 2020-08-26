// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/uber_struct_system.h"

#include <lib/syslog/cpp/macros.h>

#include <chrono>
#include <cstddef>
#include <mutex>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ui/scenic/lib/flatland/global_topology_data.h"
#include "src/ui/scenic/lib/flatland/transform_handle.h"

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
// |data| is a GlobalTopologyData object. |link_id| is the instance ID for link handles.
#define CHECK_GLOBAL_TOPOLOGY_DATA(data, link_id)    \
  {                                                  \
    std::unordered_set<TransformHandle> all_handles; \
    for (auto handle : data.topology_vector) {       \
      all_handles.insert(handle);                    \
      EXPECT_NE(handle.GetInstanceId(), link_id);    \
    }                                                \
    EXPECT_EQ(all_handles, data.live_handles);       \
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

TEST(UberStructSystemTest, RemoveSessionCleansUpSession) {
  UberStructSystem system;

  const scheduling::SessionId kSession1 = 1;
  const scheduling::SessionId kSession2 = 2;

  auto queue1 = system.AllocateQueueForSession(kSession1);
  auto queue2 = system.AllocateQueueForSession(kSession2);

  EXPECT_EQ(system.GetSessionCount(), 2ul);

  queue1->Push(0, std::make_unique<UberStruct>());
  system.ForceUpdateAllSessions();

  auto snapshot = system.Snapshot();
  EXPECT_EQ(snapshot.size(), 1ul);
  EXPECT_EQ(snapshot.count(kSession1), 1ul);
  EXPECT_EQ(snapshot.count(kSession2), 0ul);

  // Queue an UberStruct for kSession2, but don't update sessions.
  queue2->Push(0, std::make_unique<UberStruct>());

  // Remove kSession2, update sessions, and ensure the UberStruct didn't make it to the
  // InstanceMap.
  system.RemoveSession(kSession2);
  system.ForceUpdateAllSessions();

  EXPECT_EQ(system.GetSessionCount(), 1ul);

  snapshot = system.Snapshot();
  EXPECT_EQ(snapshot.size(), 1ul);
  EXPECT_EQ(snapshot.count(kSession1), 1ul);
  EXPECT_EQ(snapshot.count(kSession2), 0ul);

  // Remove kSession1 and ensure the system is empty.
  system.RemoveSession(kSession1);

  EXPECT_EQ(system.GetSessionCount(), 0ul);

  snapshot = system.Snapshot();
  EXPECT_TRUE(snapshot.empty());
}

TEST(UberStructSystemTest, UpdateSessionsTriggersSnapshotUpdate) {
  UberStructSystem system;

  // Queue empty UberStructs for two different instances and ensure the snapshot stays empty.
  // Both use the same PresentId (even though this won't happen in production).
  const scheduling::SessionId kSession1 = 1;
  const scheduling::SessionId kSession2 = 2;

  auto queue1 = system.AllocateQueueForSession(kSession1);
  auto queue2 = system.AllocateQueueForSession(kSession2);

  queue1->Push(0, std::make_unique<UberStruct>());
  queue2->Push(0, std::make_unique<UberStruct>());

  auto snapshot = system.Snapshot();
  EXPECT_TRUE(snapshot.empty());

  // Call UpdateSessions, but with only the second session, which should push that UberStruct into
  // the snapshot.
  system.UpdateSessions({{kSession2, 0}});

  snapshot = system.Snapshot();
  EXPECT_EQ(snapshot.size(), 1ul);

  auto iter = snapshot.find(kSession2);
  EXPECT_NE(iter, snapshot.end());
  EXPECT_NE(iter->second, nullptr);

  // Call it a second time with the first session, which should result in both UberStructs being in
  // the snapshot.
  system.UpdateSessions({{kSession1, 0}});

  snapshot = system.Snapshot();
  EXPECT_EQ(snapshot.size(), 2ul);

  iter = snapshot.find(kSession1);
  EXPECT_NE(iter, snapshot.end());
  EXPECT_NE(iter->second, nullptr);

  iter = snapshot.find(kSession2);
  EXPECT_NE(iter, snapshot.end());
  EXPECT_NE(iter->second, nullptr);
}

TEST(UberStructSystemTest, UpdateSessionsIgnoresGfxSessionIds) {
  UberStructSystem system;

  // Queue an UberStruct for a Flatland session and pretend there is a GFX session too.
  const scheduling::SessionId kFlatlandSession = 1;
  const scheduling::SessionId kGfxSession = 2;

  auto queue = system.AllocateQueueForSession(kFlatlandSession);

  queue->Push(0, std::make_unique<UberStruct>());

  auto snapshot = system.Snapshot();
  EXPECT_TRUE(snapshot.empty());

  // Call UpdateSessions, but with only the GFX session, which should update nothing.
  system.UpdateSessions({{kGfxSession, 0}});

  snapshot = system.Snapshot();
  EXPECT_TRUE(snapshot.empty());

  // Call it a second time with the Flatland session, which should result in an UberStruct in the
  // snapshot.
  system.UpdateSessions({{kFlatlandSession, 0}});

  snapshot = system.Snapshot();
  EXPECT_EQ(snapshot.size(), 1ul);

  auto iter = snapshot.find(kFlatlandSession);
  EXPECT_NE(iter, snapshot.end());
  EXPECT_NE(iter->second, nullptr);
}

TEST(UberStructSystemTest, UpdateSessionsConsumesPreviousPresents) {
  UberStructSystem system;

  // Make three UberStructs with different topologies.
  auto struct1 = std::make_unique<UberStruct>();
  struct1->local_topology = {{{1, 0}, 0}};

  auto struct2 = std::make_unique<UberStruct>();
  const TransformHandle kTransform2 = {2, 0};
  struct2->local_topology = {{kTransform2, 0}};

  auto struct3 = std::make_unique<UberStruct>();
  const TransformHandle kTransform3 = {3, 0};
  struct3->local_topology = {{kTransform3, 0}};

  // Queue all three in the system with incrementing PresentIds.
  const scheduling::SessionId kSession = 1;
  auto queue = system.AllocateQueueForSession(kSession);

  queue->Push(1, std::move(struct1));
  queue->Push(2, std::move(struct2));
  queue->Push(3, std::move(struct3));

  auto snapshot = system.Snapshot();
  EXPECT_TRUE(snapshot.empty());

  // Call UpdateSessions with PresentId = 2. This should skip struct1, place struct2 in the
  // snapshot, and leave struct3 queued.
  system.UpdateSessions({{kSession, 2}});

  snapshot = system.Snapshot();
  EXPECT_EQ(snapshot.size(), 1ul);

  auto iter = snapshot.find(kSession);
  EXPECT_NE(iter, snapshot.end());
  EXPECT_NE(iter->second, nullptr);
  EXPECT_EQ(iter->second->local_topology[0].handle, kTransform2);

  // Call UpdateSessions with PresentId = 3 to confirm that struct3 is still queued.
  system.UpdateSessions({{kSession, 3}});

  snapshot = system.Snapshot();
  EXPECT_EQ(snapshot.size(), 1ul);

  iter = snapshot.find(kSession);
  EXPECT_NE(iter, snapshot.end());
  EXPECT_NE(iter->second, nullptr);
  EXPECT_EQ(iter->second->local_topology[0].handle, kTransform3);

  // Ensure there are no queued updates left.
  EXPECT_EQ(queue->GetPendingSize(), 0ul);
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

  std::shared_ptr<UberStructSystem::UberStructQueue> queues[] = {
      system.AllocateQueueForSession(0),
      system.AllocateQueueForSession(1),
      system.AllocateQueueForSession(2),
  };

  std::unordered_map<scheduling::SessionId, scheduling::PresentId> sessions_to_update;
  for (const auto& v : vectors) {
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = v;

    const auto session_id = v[0].handle.GetInstanceId();
    queues[session_id]->Push(0, std::move(uber_struct));
    sessions_to_update[session_id] = 0;
  }

  system.UpdateSessions(sessions_to_update);

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
      {{{4, 0}, 2}, {link_8, 0}, {link_9, 0}},  // 4:0 - 0:8
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
      {{{1, 0}, 2}, {link_3, 0}, {link_2, 0}},  // 1:0 - 0:3
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
      {{{4, 0}, 2}, {link13, 0}, {link12, 0}},  // 4:0 - 0:13
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

  std::shared_ptr<UberStructSystem::UberStructQueue> queues[] = {
      system.AllocateQueueForSession(1),  system.AllocateQueueForSession(2),
      system.AllocateQueueForSession(3),  system.AllocateQueueForSession(4),
      system.AllocateQueueForSession(6),  system.AllocateQueueForSession(5),
      system.AllocateQueueForSession(7),  system.AllocateQueueForSession(8),
      system.AllocateQueueForSession(9),  system.AllocateQueueForSession(12),
      system.AllocateQueueForSession(13),
  };

  // Every relevant 0:X node should link to X:0.
  GlobalTopologyData::LinkTopologyMap links;
  for (uint64_t i = 2; i <= 13; ++i) {
    MakeLink(links, i);
  }

  // Initialize the graph.
  std::unordered_map<scheduling::SessionId, scheduling::PresentId> sessions_to_update;
  std::atomic<scheduling::PresentId> next_present_id = 0;

  for (size_t i = 0; i < 11; ++i) {
    const auto& v = vectors[i];
    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = v;

    const auto session_id = v[0].handle.GetInstanceId();
    const auto present_id = next_present_id++;
    queues[i]->Push(present_id, std::move(uber_struct));
    sessions_to_update[session_id] = present_id;
  }

  system.UpdateSessions(sessions_to_update);
  sessions_to_update.clear();

  // The expected output child counts should be the same regardless.
  const std::vector<uint64_t> expected_child_counts = {2, 2, 2, 0, 0, 0, 2, 2, 0, 0, 0};
  const std::vector<size_t> expected_parent_indices = {0, 0, 1, 2, 2, 1, 0, 6, 7, 7, 6};

  std::vector<std::thread> threads;
  bool run = true;

  // Only swap out the first 5 vectors, since the remaining are just leaf graphs.
  for (uint64_t t = 0; t < 5; ++t) {
    std::thread thread(
        [&run, &next_present_id, q = queues[t], v = vectors[t], a = alternate_vectors[t]]() {
          while (run) {
            {
              auto uber_struct = std::make_unique<UberStruct>();
              uber_struct->local_topology = v;

              q->Push(next_present_id++, std::move(uber_struct));
            }

            {
              auto uber_struct = std::make_unique<UberStruct>();
              uber_struct->local_topology = a;

              q->Push(next_present_id++, std::move(uber_struct));
            }
          }
        });

    threads.push_back(std::move(thread));
  }

  static constexpr uint64_t kNumChecks = 100;

  for (uint64_t i = 0; i < kNumChecks; ++i) {
    system.ForceUpdateAllSessions();

    // Because the threads always swap out each graph with an equivalent alternate graph, any
    // intermediate state, with a mix of graphs, should always produce the same set of parent
    // indexes.
    auto output = GlobalTopologyData::ComputeGlobalTopologyData(system.Snapshot(), links,
                                                                kLinkInstanceId, {1, 0});
    CHECK_GLOBAL_TOPOLOGY_DATA(output, 0u);

    EXPECT_THAT(output.child_counts, ::testing::ElementsAreArray(expected_child_counts));
    EXPECT_THAT(output.parent_indices, ::testing::ElementsAreArray(expected_parent_indices));

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
