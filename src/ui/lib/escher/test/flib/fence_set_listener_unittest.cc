// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/flib/fence_set_listener.h"

#include "gtest/gtest.h"
#include "lib/gtest/test_loop_fixture.h"
#include "src/ui/lib/escher/flib/fence.h"
#include "src/ui/lib/escher/impl/command_buffer_sequencer.h"
#include "src/ui/lib/escher/test/flib/util.h"

namespace escher {
namespace test {

class FenceSetListenerTest : public gtest::TestLoopFixture {};

TEST_F(FenceSetListenerTest, EmptySet) {
  // Create an empty FenceSetListener.
  std::vector<zx::event> fence_listeners;

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
  std::vector<zx::event> fence_listeners;
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
  std::vector<zx::event> fences;
  zx::event fence1;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &fence1));
  fences.push_back(CopyEvent(fence1));
  zx::event fence2;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &fence2));
  fences.push_back(CopyEvent(fence2));

  bool signalled = false;
  {
    FenceSetListener fence_set_listener(std::move(fences));

    // Start waiting for signal events.
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
  // We also expect the callbacks to not fire, even if we signal the fences and pump the message
  // loop.
  fence2.signal(0u, kFenceSignalled);
  RunLoopUntilIdle();
  ASSERT_FALSE(signalled);
}

TEST_F(FenceSetListenerTest, DestroyWhileNotWaiting) {
  bool signalled = false;
  {
    // Create an FenceSetListener.
    std::vector<zx::event> fences;
    FenceSetListener fence_set_listener(std::move(fences));

    // Start waiting for signal events.
    fence_set_listener.WaitReadyAsync([&signalled]() { signalled = true; });
  }
  // We expect there to be no errors while tearing down |fence_set_listener|.
  // We also expect the callbacks to not fire, even if we signal the fences and pump the message
  // loop.
  RunLoopUntilIdle();
  ASSERT_FALSE(signalled);
}

TEST_F(FenceSetListenerTest, DestroyInClosureWithEmptyFenceList) {
  bool signalled = false;
  bool deleted = false;
  {
    // Create an FenceSetListener.
    std::vector<zx::event> fences;
    auto fence_set_listener =
        std::unique_ptr<FenceSetListener, std::function<void(FenceSetListener*)>>(
            new FenceSetListener(std::move(fences)), [&deleted](FenceSetListener* listener) {
              delete listener;
              deleted = true;
            });
    fence_set_listener->WaitReadyAsync(
        [&signalled, listener = std::move(fence_set_listener)]() mutable {
          listener.reset();
          // If deleting the listener causes the closure to be deallocated, then trying to interact
          // with the local variables in the closure will trigger a use-after-free error on ASAN
          // tests.
          auto asan_check = std::move(listener);
          signalled = true;
        });
  }
  RunLoopUntilIdle();
  ASSERT_TRUE(signalled);
  ASSERT_TRUE(deleted);
}

TEST_F(FenceSetListenerTest, DestroyInClosureWithUnsignalledFence) {
  bool signalled = false;
  bool deleted = false;
  {
    // Create an FenceSetListener with one fence.
    std::vector<zx::event> fences;
    zx::event fence;
    ASSERT_EQ(ZX_OK, zx::event::create(0, &fence));
    fences.push_back(CopyEvent(fence));
    auto fence_set_listener =
        std::unique_ptr<FenceSetListener, std::function<void(FenceSetListener*)>>(
            new FenceSetListener(std::move(fences)), [&deleted](FenceSetListener* listener) {
              delete listener;
              deleted = true;
            });
    fence_set_listener->WaitReadyAsync(
        [&signalled, listener = std::move(fence_set_listener)]() mutable {
          listener.reset();
          // If deleting the listener causes the closure to be deallocated, then trying to interact
          // with the local variables in the closure will trigger a use-after-free error on ASAN
          // tests.
          auto asan_check = std::move(listener);
          signalled = true;
        });

    fence.signal(0u, kFenceSignalled);
  }
  RunLoopUntilIdle();
  ASSERT_TRUE(signalled);
  ASSERT_TRUE(deleted);
}

TEST_F(FenceSetListenerTest, DestroyInClosureWithSignalledFence) {
  bool signalled = false;
  bool deleted = false;
  {
    // Create an FenceSetListener with one fence.
    std::vector<zx::event> fences;
    zx::event fence;
    ASSERT_EQ(ZX_OK, zx::event::create(0, &fence));
    fence.signal(0u, kFenceSignalled);
    fences.push_back(CopyEvent(fence));
    auto fence_set_listener =
        std::unique_ptr<FenceSetListener, std::function<void(FenceSetListener*)>>(
            new FenceSetListener(std::move(fences)), [&deleted](FenceSetListener* listener) {
              delete listener;
              deleted = true;
            });
    fence_set_listener->WaitReadyAsync(
        [&signalled, listener = std::move(fence_set_listener)]() mutable {
          listener.reset();
          // If deleting the listener causes the closure to be deallocated, then trying to interact
          // with the local variables in the closure will trigger a use-after-free error on ASAN
          // tests.
          auto asan_check = std::move(listener);
          signalled = true;
        });
  }
  RunLoopUntilIdle();
  ASSERT_TRUE(signalled);
  ASSERT_TRUE(deleted);
}

}  // namespace test
}  // namespace escher
