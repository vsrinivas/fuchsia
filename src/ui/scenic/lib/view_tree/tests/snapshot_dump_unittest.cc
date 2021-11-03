// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/view_tree/snapshot_dump.h"

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/scenic/lib/utils/helpers.h"
#include "src/ui/scenic/lib/view_tree/tests/utils.h"

namespace view_tree {
namespace snapshot_dump::test {

// Creates a snapshot with the invalid view tree. Node has not been added to the view tree
// making it invalid
std::shared_ptr<const view_tree::Snapshot> InvalidViewTreeSnapshot() {
  auto snapshot = std::make_shared<view_tree::Snapshot>();
  snapshot->root = kNodeA;
  return snapshot;
}

// Creates a snapshot with a cycle:
//     A
//    / \
//    \ /
//     B
std::shared_ptr<const view_tree::Snapshot> CyclicalSnapshot() {
  auto snapshot = std::make_shared<view_tree::Snapshot>();

  snapshot->root = kNodeA;
  auto& view_tree = snapshot->view_tree;
  view_tree[kNodeA] = ViewNode{.parent = ZX_KOID_INVALID, .children = {kNodeB}};
  view_tree[kNodeB] = ViewNode{.parent = kNodeA, .children = {kNodeA}};
  return snapshot;
}

TEST(SnapshotDumpTest, CaptureDumpTwoNodes) {
  std::string expected_dump =
      "|[Node:1] Parent:0\n"
      " |[Node:2] Parent:1\n"
      "Hit testers# : 0\n"
      "Unconnected Views : ";
  LogDump actual_dump = SnapshotDump::DumpSnapshotInfo(TwoNodeSnapshot());
  EXPECT_EQ(actual_dump.message, expected_dump);
  EXPECT_EQ(actual_dump.status, DumpStatus::Success);
}

TEST(SnapshotDumpTest, CaptureDumpThreeNodes) {
  std::string expected_dump =
      "|[Node:1] Parent:0\n"
      " |[Node:2] Parent:1\n"
      "  |[Node:3] Parent:2\n"
      "Hit testers# : 0\n"
      "Unconnected Views : ";
  LogDump actual_dump = SnapshotDump::DumpSnapshotInfo(ThreeNodeSnapshot());
  EXPECT_EQ(actual_dump.message, expected_dump);
  EXPECT_EQ(actual_dump.status, DumpStatus::Success);
}

TEST(SnapshotDumpTest, CaptureDumpFourNodes) {
  std::string expected_dump =
      "|[Node:1] Parent:0\n"
      " |[Node:2] Parent:1\n"
      "  |[Node:4] Parent:2\n"
      " |[Node:3] Parent:1\n"
      "Hit testers# : 0\n"
      "Unconnected Views : ";
  LogDump actual_dump = SnapshotDump::DumpSnapshotInfo(FourNodeSnapshot());
  EXPECT_EQ(actual_dump.message, expected_dump);
  EXPECT_EQ(actual_dump.status, DumpStatus::Success);
}

TEST(SnapshotDumpTest, InvalidSnapshotTest) {
  LogDump actual_dump = SnapshotDump::DumpSnapshotInfo(nullptr);
  EXPECT_EQ(actual_dump.status, DumpStatus::Failure);
}

TEST(SnapshotDumpTest, InvalidViewTreeSnapshotTest) {
  LogDump actual_dump = SnapshotDump::DumpSnapshotInfo(InvalidViewTreeSnapshot());
  EXPECT_EQ(actual_dump.status, DumpStatus::Failure);
}

TEST(SnapshotDumpTest, CycleDetectionTest) {
  LogDump actual_dump = SnapshotDump::DumpSnapshotInfo(CyclicalSnapshot());
  EXPECT_EQ(actual_dump.status, DumpStatus::Failure);
}
}  // namespace snapshot_dump::test
}  // namespace view_tree
