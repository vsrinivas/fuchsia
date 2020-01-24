// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "task_tree.h"

#include <zircon/process.h>

#include "gtest/gtest.h"
#include "root_resource.h"

class TaskTreeForTesting : public ::harvester::TaskTree {
 public:
  std::map<zx_koid_t, zx_handle_t> KoidsToHandles() {
    return koids_to_handles_;
  }

  void ClearForTesting() { Clear(); }
};

class TaskTreeTest : public ::testing::Test {
 public:
  void SetUp() override {}

  TaskTreeForTesting& tree() { return task_tree_; }

 private:
  TaskTreeForTesting task_tree_;
};

TEST_F(TaskTreeTest, Test) {
  EXPECT_EQ(0U, tree().Jobs().size());
  EXPECT_EQ(0U, tree().Processes().size());
  EXPECT_EQ(0U, tree().Threads().size());

  tree().Gather();
  EXPECT_NE(0U, tree().Jobs().size());
  EXPECT_NE(0U, tree().Processes().size());
  EXPECT_NE(0U, tree().Threads().size());

  // The tree is walked from the root job. So as we iterate over the jobs,
  // processes, and threads (in that order) well will have visited the parent
  // of each object. Use this set to see that the parent exists.
  std::set<zx_koid_t> koids;
  bool found_root = false;

  for (const auto& entry : tree().Jobs()) {
    EXPECT_NE(ZX_HANDLE_INVALID, entry.handle);
    if (entry.koid == 0U) {
      found_root = true;
    } else {
      // This job's parent should be in the set.
      EXPECT_NE(koids.end(), koids.find(entry.parent_koid));
      EXPECT_NE(entry.koid, entry.parent_koid);
    }

    EXPECT_EQ(koids.end(), koids.find(entry.koid));
    koids.emplace(entry.koid);
  }

  EXPECT_TRUE(found_root);

  for (const auto& entry : tree().Processes()) {
    EXPECT_NE(ZX_HANDLE_INVALID, entry.handle);
    EXPECT_NE(0U, entry.koid);
    EXPECT_NE(entry.koid, entry.parent_koid);
    // This process's parent should be in the set.
    EXPECT_NE(koids.end(), koids.find(entry.parent_koid));

    EXPECT_EQ(koids.end(), koids.find(entry.koid));
    koids.emplace(entry.koid);
  }

  for (const auto& entry : tree().Threads()) {
    EXPECT_NE(ZX_HANDLE_INVALID, entry.handle);
    EXPECT_NE(0U, entry.koid);
    EXPECT_NE(0U, entry.parent_koid);
    EXPECT_NE(entry.koid, entry.parent_koid);
    // This thread's parent should be in the set.
    EXPECT_NE(koids.end(), koids.find(entry.parent_koid));

    EXPECT_EQ(koids.end(), koids.find(entry.koid));
    koids.emplace(entry.koid);
  }

  size_t total_entries = tree().Jobs().size() + tree().Processes().size() +
                         tree().Threads().size();
  EXPECT_EQ(total_entries, koids.size());
  EXPECT_EQ(total_entries, tree().KoidsToHandles().size());

  // Our koid should appear somewhere in the list.
  zx_info_handle_basic_t info;
  zx_status_t status = zx_object_get_info(
      zx_process_self(), ZX_INFO_HANDLE_BASIC, &info, sizeof(info),
      /*actual=*/nullptr, /*avail=*/nullptr);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_NE(koids.end(), koids.find(info.koid));

  tree().ClearForTesting();
  EXPECT_EQ(0U, tree().Jobs().size());
  EXPECT_EQ(0U, tree().Processes().size());
  EXPECT_EQ(0U, tree().Threads().size());
  EXPECT_EQ(0U, tree().KoidsToHandles().size());
}
