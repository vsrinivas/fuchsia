// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/simple_camera/simple_camera_lib/buffer_fence.h"

#include "gtest/gtest.h"
#include "lib/gtest/test_loop_fixture.h"

namespace simple_camera::test {

using BufferFenceTest = ::gtest::TestLoopFixture;

TEST_F(BufferFenceTest, SmokeTest) {
  uint32_t signalled_index = 0;
  static constexpr uint32_t kFenceIndex = 5;

  std::unique_ptr<BufferFence> bf = BufferFence::Create(kFenceIndex);

  zx::event release_fence;
  bf->DuplicateReleaseFence(&release_fence);

  bf->SetReleaseFenceHandler([&signalled_index](BufferFence* b) { signalled_index = b->index(); });

  // signal the fence so we will get a callback
  release_fence.signal(0, ZX_EVENT_SIGNALED);

  // run the dispatcher to make that callback happen
  RunLoopUntilIdle();
  EXPECT_EQ(kFenceIndex, signalled_index);

  // And again, to make sure things are reset properly:
  signalled_index = 0;
  // signal the fence so we will get a callback
  release_fence.signal(0, ZX_EVENT_SIGNALED);

  // run the dispatcher to make that callback happen
  RunLoopUntilIdle();
  EXPECT_EQ(kFenceIndex, signalled_index);
}

}  // namespace simple_camera::test
