// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/flib/release_fence_signaller.h"

#include <gtest/gtest.h>

#include "src/ui/lib/escher/flib/fence.h"
#include "src/ui/lib/escher/impl/command_buffer_sequencer.h"
#include "src/ui/lib/escher/test/flib/util.h"

namespace escher {
namespace test {

class ReleaseFenceSignallerTest : public ::testing::Test {
 public:
  uint64_t GenerateNextCommandBufferSequenceNumber(impl::CommandBufferSequencer* sequencer) {
    return sequencer->GenerateNextCommandBufferSequenceNumber();
  }
  void CommandBufferFinished(impl::CommandBufferSequencer* sequencer, uint64_t num) {
    sequencer->CommandBufferFinished(num);
  }
};

TEST_F(ReleaseFenceSignallerTest, FencesSignalledProperly) {
  escher::impl::CommandBufferSequencer sequencer;
  ReleaseFenceSignaller release_fence_signaler(&sequencer);

  // Create two fences.
  uint64_t seq_num1 = GenerateNextCommandBufferSequenceNumber(&sequencer);
  zx::event fence1;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &fence1));
  release_fence_signaler.AddCPUReleaseFence(CopyEvent(fence1));

  uint64_t seq_num2 = GenerateNextCommandBufferSequenceNumber(&sequencer);
  zx::event fence2;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &fence2));
  release_fence_signaler.AddCPUReleaseFence(CopyEvent(fence2));

  // Create a third fence that will not be signaled initially.
  uint64_t seq_num3 = GenerateNextCommandBufferSequenceNumber(&sequencer);
  zx::event fence3;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &fence3));
  release_fence_signaler.AddCPUReleaseFence(CopyEvent(fence3));

  // Assert that none of the fences are signalled.
  ASSERT_FALSE(IsEventSignalled(fence1, kFenceSignalled));
  ASSERT_FALSE(IsEventSignalled(fence2, kFenceSignalled));

  // Mark the sequence numbers so far as finished. (Do it out of order for fun).
  CommandBufferFinished(&sequencer, seq_num2);
  CommandBufferFinished(&sequencer, seq_num1);

  ASSERT_TRUE(IsEventSignalled(fence1, kFenceSignalled));
  ASSERT_TRUE(IsEventSignalled(fence2, kFenceSignalled));
  ASSERT_FALSE(IsEventSignalled(fence3, kFenceSignalled));

  CommandBufferFinished(&sequencer, seq_num3);

  ASSERT_TRUE(IsEventSignalled(fence1, kFenceSignalled));
  ASSERT_TRUE(IsEventSignalled(fence2, kFenceSignalled));
  ASSERT_TRUE(IsEventSignalled(fence3, kFenceSignalled));
}

}  // namespace test
}  // namespace escher
