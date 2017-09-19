// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/command_buffer_sequencer.h"
#include "gtest/gtest.h"

#include "garnet/bin/ui/scene_manager/sync/acquire_fence.h"
#include "garnet/bin/ui/scene_manager/sync/fence.h"
#include "garnet/bin/ui/scene_manager/tests/util.h"
#include "lib/ui/tests/test_with_message_loop.h"

namespace scene_manager {
namespace test {

class AcquireFenceTest : public ::testing::Test {};

TEST_F(AcquireFenceTest, SimpleAcquireFenceSignalling) {
  // Create an AcquireFence.
  zx::event fence1;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &fence1));
  AcquireFence buffer_fence1(CopyEvent(fence1));

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

TEST_F(AcquireFenceTest, AsyncAcquireFenceSignalling) {
  // Create an AcquireFence.
  zx::event fence1;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &fence1));
  AcquireFence buffer_fence1(CopyEvent(fence1));

  // Expect that it is not signalled initially.
  EXPECT_FALSE(buffer_fence1.WaitReady(fxl::TimeDelta::Zero()));
  EXPECT_FALSE(buffer_fence1.ready());

  bool signalled = false;
  // Expect that it is signalled now.
  buffer_fence1.WaitReadyAsync([&signalled]() { signalled = true; });

  // Signal the fence.
  fence1.signal(0u, kFenceSignalled);

  RUN_MESSAGE_LOOP_UNTIL(buffer_fence1.ready());
  EXPECT_TRUE(signalled);
}

}  // namespace test
}  // namespace scene_manager
