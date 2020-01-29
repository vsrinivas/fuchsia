// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/flib/fence_listener.h"

#include "gtest/gtest.h"
#include "lib/gtest/test_loop_fixture.h"
#include "src/ui/lib/escher/flib/fence.h"
#include "src/ui/lib/escher/impl/command_buffer_sequencer.h"
#include "src/ui/lib/escher/test/flib/util.h"

namespace escher {
namespace test {

class FenceListenerTest : public gtest::TestLoopFixture {};

TEST_F(FenceListenerTest, SimpleFenceListenerSignalling) {
  // Create an FenceListener.
  zx::event fence1;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &fence1));
  FenceListener buffer_fence1(CopyEvent(fence1));

  // Expect that it is not signaled initially.
  EXPECT_FALSE(buffer_fence1.ready());
  EXPECT_FALSE(buffer_fence1.WaitReady(fxl::TimeDelta::Zero()));

  // Still should not be ready.
  EXPECT_FALSE(buffer_fence1.ready());

  // Signal the fence.
  fence1.signal(0u, kFenceSignalled);

  // Expect that it is signaled now.
  EXPECT_TRUE(buffer_fence1.WaitReady(fxl::TimeDelta::Zero()));
  EXPECT_TRUE(buffer_fence1.ready());
}

TEST_F(FenceListenerTest, AsyncFenceListenerSignalling) {
  // Create an FenceListener.
  zx::event fence1;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &fence1));
  FenceListener buffer_fence1(CopyEvent(fence1));

  // Expect that it is not signaled initially.
  EXPECT_FALSE(buffer_fence1.WaitReady(fxl::TimeDelta::Zero()));
  EXPECT_FALSE(buffer_fence1.ready());

  bool signalled = false;
  // Expect that it is signaled now.
  buffer_fence1.WaitReadyAsync([&signalled]() { signalled = true; });

  // Signal the fence.
  fence1.signal(0u, kFenceSignalled);

  RunLoopUntilIdle();
  EXPECT_TRUE(buffer_fence1.ready());
  EXPECT_TRUE(signalled);
}

TEST_F(FenceListenerTest, DestroyWhileWaiting) {
  zx::event fence;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &fence));

  bool signalled = false;
  {
    // Create a FenceSetListener.
    FenceListener fence_listener(CopyEvent(fence));

    // Start waiting for signal events.
    fence_listener.WaitReadyAsync([&signalled]() { signalled = true; });

    // Expect that the set is not ready initially. Briefly pump the message
    // loop, although we don't expect anything to be handled.
    RunLoopUntilIdle();
    EXPECT_FALSE(fence_listener.ready());
    EXPECT_FALSE(signalled);

    // Signal one fence, then delete the the listener.
    fence.signal(0u, kFenceSignalled);
  }
  // We expect there to be no errors while tearing down |fence_listener|. We also expect the
  // callbacks to not fire, even if we pump the message loop again.
  RunLoopUntilIdle();
  ASSERT_FALSE(signalled);
}

TEST_F(FenceListenerTest, DestroyWhileNotWaiting) {
  zx::event fence;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &fence));
  // Signal the fence immediately.
  fence.signal(0u, kFenceSignalled);

  bool signalled = false;
  {
    // Create a FenceSetListener.
    FenceListener fence_listener(CopyEvent(fence));
    // Start waiting for signal events.
    fence_listener.WaitReadyAsync([&signalled]() { signalled = true; });
    // Delete the the listener.
  }
  // We expect there to be no errors while tearing down |fence_listener|. We also expect the
  // callbacks to not fire, even if we pump the message loop again.
  RunLoopUntilIdle();
  EXPECT_FALSE(signalled);
}

TEST_F(FenceListenerTest, DestroyInClosurePresignalled) {
  zx::event fence;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &fence));
  // Signal the fence immediately.
  fence.signal(0u, kFenceSignalled);

  bool signalled = false;
  bool deleted = false;
  {
    // This is a unique pointer with a custom deleter, so we can detect that deletion has occurred
    // in this test.
    auto fence_listener = std::unique_ptr<FenceListener, std::function<void(FenceListener*)>>(
        new FenceListener(CopyEvent(fence)), [&deleted](FenceListener* listener) {
          delete listener;
          deleted = true;
        });
    fence_listener->WaitReadyAsync([&signalled, listener = std::move(fence_listener)]() mutable {
      listener.reset();
      // If deleting the listener causes the closure to be deallocated, then trying to interact with
      // the local variables in the closure will trigger a use-after-free error on ASAN tests.
      auto asan_check = std::move(listener);
      signalled = true;
    });
  }
  RunLoopUntilIdle();
  EXPECT_TRUE(signalled);
  EXPECT_TRUE(deleted);
}

TEST_F(FenceListenerTest, DestroyInClosurePostsignalled) {
  zx::event fence;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &fence));
  // Signal the fence immediately.

  bool signalled = false;
  bool deleted = false;
  {
    // This is a unique pointer with a custom deleter, so we can detect that deletion has occurred
    // in this test.
    auto fence_listener = std::unique_ptr<FenceListener, std::function<void(FenceListener*)>>(
        new FenceListener(CopyEvent(fence)), [&deleted](FenceListener* listener) {
          delete listener;
          deleted = true;
        });
    fence_listener->WaitReadyAsync([&signalled, listener = std::move(fence_listener)]() mutable {
      listener.reset();
      // If deleting the listener causes the closure to be deallocated, then trying to interact
      // with the local variables in the closure will trigger a use-after-free error on ASAN
      // tests.
      auto asan_check = std::move(listener);
      signalled = true;
    });

    // Signal the fence after calling WaitReadyAsync().
    fence.signal(0u, kFenceSignalled);
  }
  RunLoopUntilIdle();
  EXPECT_TRUE(signalled);
  EXPECT_TRUE(deleted);
}

}  // namespace test
}  // namespace escher
