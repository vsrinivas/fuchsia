// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "task_tree.h"

#include <zircon/process.h>

#include "gtest/gtest.h"
#include "root_resource.h"

class TaskTreeTest : public ::testing::Test {
 public:
  void SetUp() override {}

  harvester::TaskTree& tree() { return task_tree_; }

 private:
  harvester::TaskTree task_tree_;
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
    EXPECT_NE(0U, entry.koid);
    if (entry.parent_koid == 0U) {
      // Each tree will have a single root with a parent_koid of zero.
      found_root = true;
    } else {
      // This job' parent should be in the set.
      EXPECT_NE(koids.end(), koids.find(entry.parent_koid));
      // No other object will have a zero parent koid.
      EXPECT_NE(0U, entry.parent_koid);
    }
    EXPECT_NE(entry.koid, entry.parent_koid);

    EXPECT_EQ(koids.end(), koids.find(entry.koid));
    koids.emplace(entry.koid);
  }

  for (const auto& entry : tree().Processes()) {
    EXPECT_NE(ZX_HANDLE_INVALID, entry.handle);
    EXPECT_NE(0U, entry.koid);
    EXPECT_NE(0U, entry.parent_koid);
    EXPECT_NE(entry.koid, entry.parent_koid);
    // This process' parent should be in the set.
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

  tree().Clear();
  EXPECT_EQ(0U, tree().Jobs().size());
  EXPECT_EQ(0U, tree().Processes().size());
  EXPECT_EQ(0U, tree().Threads().size());
}
