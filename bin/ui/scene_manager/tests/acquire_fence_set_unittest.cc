// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/command_buffer_sequencer.h"
#include "gtest/gtest.h"

#include "apps/mozart/lib/tests/test_with_message_loop.h"
#include "apps/mozart/src/scene_manager/acquire_fence_set.h"
#include "apps/mozart/src/scene_manager/fence.h"
#include "apps/mozart/src/scene_manager/tests/util.h"

namespace scene_manager {
namespace test {

class AcquireFenceSetTest : public ::testing::Test {};

TEST_F(AcquireFenceSetTest, EmptySet) {
  // Create an empty AcquireFenceSet.
  ::fidl::Array<mx::event> acquire_fences;

  AcquireFenceSet acquire_fence_set(std::move(acquire_fences));

  // Start waiting for signal events.
  bool signalled = false;
  acquire_fence_set.WaitReadyAsync([&signalled]() { signalled = true; });

  // Assert that the set is signalled.
  ASSERT_TRUE(acquire_fence_set.ready());
  RUN_MESSAGE_LOOP_UNTIL(signalled);
}

TEST_F(AcquireFenceSetTest, ReadyStateSignalled) {
  // Create an AcquireFenceSet.
  ::fidl::Array<mx::event> acquire_fences;
  mx::event fence1;
  ASSERT_EQ(MX_OK, mx::event::create(0, &fence1));
  acquire_fences.push_back(CopyEvent(fence1));
  mx::event fence2;
  ASSERT_EQ(MX_OK, mx::event::create(0, &fence2));
  acquire_fences.push_back(CopyEvent(fence2));
  mx::event fence3;
  ASSERT_EQ(MX_OK, mx::event::create(0, &fence3));
  acquire_fences.push_back(CopyEvent(fence3));

  AcquireFenceSet acquire_fence_set(std::move(acquire_fences));

  // Start waiting for signal events.
  bool signalled = false;
  acquire_fence_set.WaitReadyAsync([&signalled]() { signalled = true; });

  // Expect that the set is not ready initially. Briefly pump the message loop,
  // although we don't expect anything to be handled.
  ::mozart::test::RunLoopWithTimeout(kPumpMessageLoopDuration);
  ASSERT_FALSE(acquire_fence_set.ready());
  ASSERT_FALSE(signalled);

  // Signal one fence.
  fence1.signal(0u, kFenceSignalled);

  // Briefly pump the message loop, but we expect that the set is still not
  // ready.
  ::mozart::test::RunLoopWithTimeout(kPumpMessageLoopDuration);
  ASSERT_FALSE(acquire_fence_set.ready());
  ASSERT_FALSE(signalled);

  // Signal the second and third fence.
  fence2.signal(0u, kFenceSignalled);
  fence3.signal(0u, kFenceSignalled);

  // Assert that the set is now signalled.
  RUN_MESSAGE_LOOP_UNTIL(acquire_fence_set.ready());
  ASSERT_TRUE(signalled);
}

TEST_F(AcquireFenceSetTest, DestroyWhileWaiting) {
  // Create an AcquireFenceSet.
  ::fidl::Array<mx::event> acquire_fences;
  mx::event fence1;
  ASSERT_EQ(MX_OK, mx::event::create(0, &fence1));
  acquire_fences.push_back(CopyEvent(fence1));
  mx::event fence2;
  ASSERT_EQ(MX_OK, mx::event::create(0, &fence2));
  acquire_fences.push_back(CopyEvent(fence2));

  {
    AcquireFenceSet acquire_fence_set(std::move(acquire_fences));

    // Start waiting for signal events.
    bool signalled = false;
    acquire_fence_set.WaitReadyAsync([&signalled]() { signalled = true; });

    // Expect that the set is not ready initially. Briefly pump the message
    // loop, although we don't expect anything to be handled.
    ::mozart::test::RunLoopWithTimeout(kPumpMessageLoopDuration);
    ASSERT_FALSE(acquire_fence_set.ready());
    ASSERT_FALSE(signalled);

    // Signal one fence.
    fence1.signal(0u, kFenceSignalled);

    // Briefly pump the message loop, but we expect that the set is still not
    // ready.
    ::mozart::test::RunLoopWithTimeout(kPumpMessageLoopDuration);
    ASSERT_FALSE(acquire_fence_set.ready());
    ASSERT_FALSE(signalled);
  }
  // We expect there to be no errors while tearing down |acquire_fence_set|.
}

}  // namespace test
}  // namespace scene_manager
