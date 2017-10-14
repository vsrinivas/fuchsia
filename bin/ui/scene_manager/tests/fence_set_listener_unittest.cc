// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "lib/escher/impl/command_buffer_sequencer.h"

#include "garnet/bin/ui/scene_manager/sync/fence.h"
#include "garnet/bin/ui/scene_manager/sync/fence_set_listener.h"
#include "garnet/bin/ui/scene_manager/tests/util.h"
#include "lib/ui/tests/test_with_message_loop.h"

namespace scene_manager {
namespace test {

class FenceSetListenerTest : public ::testing::Test {};

TEST_F(FenceSetListenerTest, EmptySet) {
  // Create an empty FenceSetListener.
  ::fidl::Array<zx::event> fence_listeners;

  FenceSetListener fence_set_listener(std::move(fence_listeners));

  // Start waiting for signal events.
  bool signalled = false;
  fence_set_listener.WaitReadyAsync([&signalled]() { signalled = true; });

  // Assert that the set is signalled.
  ASSERT_TRUE(fence_set_listener.ready());
  RUN_MESSAGE_LOOP_UNTIL(signalled);
}

TEST_F(FenceSetListenerTest, ReadyStateSignalled) {
  // Create an FenceSetListener.
  ::fidl::Array<zx::event> fence_listeners;
  zx::event fence1;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &fence1));
  fence_listeners.push_back(CopyEvent(fence1));
  zx::event fence2;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &fence2));
  fence_listeners.push_back(CopyEvent(fence2));
  zx::event fence3;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &fence3));
  fence_listeners.push_back(CopyEvent(fence3));

  FenceSetListener fence_set_listener(std::move(fence_listeners));

  // Start waiting for signal events.
  bool signalled = false;
  fence_set_listener.WaitReadyAsync([&signalled]() { signalled = true; });

  // Expect that the set is not ready initially. Briefly pump the message loop,
  // although we don't expect anything to be handled.
  ::mozart::test::RunLoopWithTimeout(kPumpMessageLoopDuration);
  ASSERT_FALSE(fence_set_listener.ready());
  ASSERT_FALSE(signalled);

  // Signal one fence.
  fence1.signal(0u, kFenceSignalled);

  // Briefly pump the message loop, but we expect that the set is still not
  // ready.
  ::mozart::test::RunLoopWithTimeout(kPumpMessageLoopDuration);
  ASSERT_FALSE(fence_set_listener.ready());
  ASSERT_FALSE(signalled);

  // Signal the second and third fence.
  fence2.signal(0u, kFenceSignalled);
  fence3.signal(0u, kFenceSignalled);

  // Assert that the set is now signalled.
  RUN_MESSAGE_LOOP_UNTIL(fence_set_listener.ready());
  ASSERT_TRUE(signalled);
}

TEST_F(FenceSetListenerTest, DestroyWhileWaiting) {
  // Create an FenceSetListener.
  ::fidl::Array<zx::event> fence_listeners;
  zx::event fence1;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &fence1));
  fence_listeners.push_back(CopyEvent(fence1));
  zx::event fence2;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &fence2));
  fence_listeners.push_back(CopyEvent(fence2));

  {
    FenceSetListener fence_set_listener(std::move(fence_listeners));

    // Start waiting for signal events.
    bool signalled = false;
    fence_set_listener.WaitReadyAsync([&signalled]() { signalled = true; });

    // Expect that the set is not ready initially. Briefly pump the message
    // loop, although we don't expect anything to be handled.
    ::mozart::test::RunLoopWithTimeout(kPumpMessageLoopDuration);
    ASSERT_FALSE(fence_set_listener.ready());
    ASSERT_FALSE(signalled);

    // Signal one fence.
    fence1.signal(0u, kFenceSignalled);

    // Briefly pump the message loop, but we expect that the set is still not
    // ready.
    ::mozart::test::RunLoopWithTimeout(kPumpMessageLoopDuration);
    ASSERT_FALSE(fence_set_listener.ready());
    ASSERT_FALSE(signalled);
  }
  // We expect there to be no errors while tearing down |fence_set_listener|.
}

}  // namespace test
}  // namespace scene_manager
