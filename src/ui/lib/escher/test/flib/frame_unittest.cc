// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/renderer/frame.h"

#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>

#include <gtest/gtest.h>

#include "lib/gtest/test_loop_fixture.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/test/flib/util.h"
#include "src/ui/lib/escher/util/fuchsia_utils.h"

namespace escher {
namespace test {

using FrameTest = test::TestWithVkValidationLayer;

VK_TEST_F(FrameTest, SubmitFrameWithUnsignalledWaitSemaphore) {
  // TODO(fxbug.dev/58325): The emulator will block if a command queue with a pending fence is
  // submitted. So this test, which depends on a delayed GPU execution, would deadlock.
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);

  async::TestLoop loop;
  auto escher = test::GetEscher()->GetWeakPtr();
  auto frame = escher->NewFrame("test_frame", 0, false, CommandBuffer::Type::kGraphics);

  // Add a wait semaphore.
  auto acquire_semaphore_pair = escher::NewSemaphoreEventPair(escher.get());
  frame->cmds()->AddWaitSemaphore(acquire_semaphore_pair.first,
                                  vk::PipelineStageFlagBits::eTopOfPipe);
  EXPECT_FALSE(IsEventSignalled(acquire_semaphore_pair.second, ZX_EVENT_SIGNALED));

  // Add a release semaphore.
  auto release_semaphore_pair = escher::NewSemaphoreEventPair(escher.get());
  frame->cmds()->AddSignalSemaphore(release_semaphore_pair.first);
  EXPECT_FALSE(IsEventSignalled(release_semaphore_pair.second, ZX_EVENT_SIGNALED));

  // Submit frame while wait semaphore is not signalled.
  frame->EndFrame(SemaphorePtr(), [] {});
  EXPECT_FALSE(IsEventSignalled(acquire_semaphore_pair.second, ZX_EVENT_SIGNALED));

  // Release semaphore should not be signaled yet.
  EXPECT_NE(release_semaphore_pair.second.wait_one(ZX_EVENT_SIGNALED,
                                                   zx::deadline_after(zx::sec(1)), nullptr),
            ZX_OK);

  // Signal wait semaphore.
  EXPECT_EQ(acquire_semaphore_pair.second.signal(0u, ZX_EVENT_SIGNALED), ZX_OK);

  // Release semaphore should be signaled and acquire semaphore should be unsignaled by vk. We
  // should not wait more than 1 sec, because the driver can decide to signal the hung semaphore
  // after some time.
  EXPECT_EQ(release_semaphore_pair.second.wait_one(ZX_EVENT_SIGNALED,
                                                   zx::deadline_after(zx::sec(1)), nullptr),
            ZX_OK);
  loop.RunUntilIdle();
  EXPECT_FALSE(IsEventSignalled(acquire_semaphore_pair.second, ZX_EVENT_SIGNALED));
  EXPECT_TRUE(IsEventSignalled(release_semaphore_pair.second, ZX_EVENT_SIGNALED));

  // Cleanup
  escher->vk_device().waitIdle();
  loop.RunUntilIdle();
}

}  // namespace test
}  // namespace escher
