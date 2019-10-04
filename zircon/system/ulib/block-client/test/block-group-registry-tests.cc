// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <block-client/cpp/block-group-registry.h>

#include <thread>

#include <zxtest/zxtest.h>

namespace block_client {
namespace {

TEST(BlockGroupRegistryTest, AssignOneGroupID) {
  BlockGroupRegistry registry;

  EXPECT_EQ(0, registry.GroupID());
  EXPECT_EQ(0, registry.GroupID());
}

TEST(BlockGroupRegistryTest, AssignMultipleGroups) {
  BlockGroupRegistry registry;

  // Ensure primary calling thread has an assigned group first.
  ASSERT_EQ(0, registry.GroupID());

  std::thread background_thread([&registry]() { EXPECT_EQ(1, registry.GroupID()); });

  // Although the background group is different, the current thread's
  // group should remain the same.
  EXPECT_EQ(0, registry.GroupID());
  background_thread.join();
  EXPECT_EQ(0, registry.GroupID());
}

TEST(BlockGroupRegistryTest, GroupsResetWithNewRegistry) {
  // Setup:
  // - Calling thread has groupID = 0.
  // - Background thread has groupID = 1.
  {
    BlockGroupRegistry registry;
    ASSERT_EQ(0, registry.GroupID());
    std::thread background_thread([&registry]() { EXPECT_EQ(1, registry.GroupID()); });
    background_thread.join();
  }

  ASSERT_NO_FAILURES();

  // With a new instance of the registry, observe we can change the group ID of the
  // calling thread.
  {
    BlockGroupRegistry registry;
    std::thread background_thread([&registry]() { EXPECT_EQ(0, registry.GroupID()); });
    background_thread.join();
    ASSERT_EQ(1, registry.GroupID());
  }
}

}  // namespace
}  // namespace block_client
