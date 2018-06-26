// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#include "lib/escher/flib/fence.h"
#include "lib/escher/flib/fence_set_listener.h"
#include "lib/escher/impl/command_buffer_sequencer.h"
#include "lib/escher/test/flib/util.h"
#include "lib/gtest/test_loop_fixture.h"

namespace escher {
namespace test {

class FenceSetListenerTest : public gtest::TestLoopFixture {};

TEST_F(FenceSetListenerTest, EmptySet) {
  // Create an empty FenceSetListener.
  ::fidl::VectorPtr<zx::event> fence_listeners;

  FenceSetListener fence_set_listener(std::move(fence_listeners));

  // Start waiting for signal events.
  bool signalled = false;
  fence_set_listener.WaitReadyAsync([&signalled]() { signalled = true; });

  // Assert that the set is signalled.
  ASSERT_TRUE(fence_set_listener.ready());
  RunLoopUntilIdle();
  EXPECT_TRUE(signalled);
}

TEST_F(FenceSetListenerTest, ReadyStateSignalled) {
  // Create an FenceSetListener.
  ::fidl::VectorPtr<zx::event> fence_listeners;
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
  RunLoopUntilIdle();
  ASSERT_FALSE(fence_set_listener.ready());
  ASSERT_FALSE(signalled);

  // Signal one fence.
  fence1.signal(0u, kFenceSignalled);

  // Briefly pump the message loop, but we expect that the set is still not
  // ready.
  RunLoopUntilIdle();
  ASSERT_FALSE(fence_set_listener.ready());
  ASSERT_FALSE(signalled);

  // Signal the second and third fence.
  fence2.signal(0u, kFenceSignalled);
  fence3.signal(0u, kFenceSignalled);

  // Assert that the set is now signalled.
  RunLoopUntilIdle();
  EXPECT_TRUE(fence_set_listener.ready());
  ASSERT_TRUE(signalled);
}

TEST_F(FenceSetListenerTest, DestroyWhileWaiting) {
  // Create an FenceSetListener.
  ::fidl::VectorPtr<zx::event> fence_listeners;
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
    RunLoopUntilIdle();
    ASSERT_FALSE(fence_set_listener.ready());
    ASSERT_FALSE(signalled);

    // Signal one fence.
    fence1.signal(0u, kFenceSignalled);

    // Briefly pump the message loop, but we expect that the set is still not
    // ready.
    RunLoopUntilIdle();
    ASSERT_FALSE(fence_set_listener.ready());
    ASSERT_FALSE(signalled);
  }
  // We expect there to be no errors while tearing down |fence_set_listener|.
}

}  // namespace test
}  // namespace escher
