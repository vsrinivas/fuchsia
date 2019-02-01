// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#include "lib/escher/flib/fence.h"
#include "lib/escher/flib/fence_listener.h"
#include "lib/escher/impl/command_buffer_sequencer.h"
#include "lib/escher/test/flib/util.h"
#include "lib/gtest/test_loop_fixture.h"

namespace escher {
namespace test {

class FenceListenerTest : public gtest::TestLoopFixture {};

TEST_F(FenceListenerTest, SimpleFenceListenerSignalling) {
  // Create an FenceListener.
  zx::event fence1;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &fence1));
  FenceListener buffer_fence1(CopyEvent(fence1));

  // Expect that it is not signalled initially.
  EXPECT_FALSE(buffer_fence1.ready());
  EXPECT_FALSE(buffer_fence1.WaitReady(fxl::TimeDelta::Zero()));

  // Still should not be ready.
  EXPECT_FALSE(buffer_fence1.ready());

  // Signal the fence.
  fence1.signal(0u, kFenceSignalled);

  // Expect that it is signalled now.
  EXPECT_TRUE(buffer_fence1.WaitReady(fxl::TimeDelta::Zero()));
  EXPECT_TRUE(buffer_fence1.ready());
}

TEST_F(FenceListenerTest, AsyncFenceListenerSignalling) {
  // Create an FenceListener.
  zx::event fence1;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &fence1));
  FenceListener buffer_fence1(CopyEvent(fence1));

  // Expect that it is not signalled initially.
  EXPECT_FALSE(buffer_fence1.WaitReady(fxl::TimeDelta::Zero()));
  EXPECT_FALSE(buffer_fence1.ready());

  bool signalled = false;
  // Expect that it is signalled now.
  buffer_fence1.WaitReadyAsync([&signalled]() { signalled = true; });

  // Signal the fence.
  fence1.signal(0u, kFenceSignalled);

  RunLoopUntilIdle();
  EXPECT_TRUE(buffer_fence1.ready());
  EXPECT_TRUE(signalled);
}

}  // namespace test
}  // namespace escher
