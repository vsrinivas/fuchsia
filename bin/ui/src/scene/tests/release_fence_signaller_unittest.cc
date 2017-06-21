// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/release_fence_signaller.h"
#include "escher/impl/command_buffer_sequencer.h"
#include "gtest/gtest.h"

namespace mozart {
namespace scene {
namespace test {

using ReleaseFenceSignallerTest = ::testing::Test;

bool IsFenceSignalled(const mx::event& fence) {
  mx_signals_t pending = 0u;
  fence.wait_one(kReleaseFenceSignal, 0, &pending);
  return (pending & kReleaseFenceSignal) != 0u;
}

TEST_F(ReleaseFenceSignallerTest, FencesSignalledProperly) {
  escher::impl::CommandBufferSequencer sequencer;
  ReleaseFenceSignaller release_fence_signaler(&sequencer);

  // Create two fences.
  uint64_t seq_num1 = sequencer.GetNextCommandBufferSequenceNumber();
  mx::event fence1;
  ASSERT_EQ(mx::event::create(0, &fence1), MX_OK);
  mx::event temp_fence1;
  ASSERT_EQ(fence1.duplicate(MX_RIGHT_SAME_RIGHTS, &temp_fence1), MX_OK);
  release_fence_signaler.AddCPUReleaseFence(std::move(temp_fence1));

  uint64_t seq_num2 = sequencer.GetNextCommandBufferSequenceNumber();
  mx::event fence2;
  ASSERT_EQ(mx::event::create(0, &fence2), MX_OK);
  mx::event temp_fence2;
  ASSERT_EQ(fence2.duplicate(MX_RIGHT_SAME_RIGHTS, &temp_fence2), MX_OK);
  release_fence_signaler.AddCPUReleaseFence(std::move(temp_fence2));

  // Create a third fence that will not be signaled initially.
  uint64_t seq_num3 = sequencer.GetNextCommandBufferSequenceNumber();
  mx::event fence3;
  ASSERT_EQ(mx::event::create(0, &fence3), MX_OK);
  mx::event temp_fence3;
  ASSERT_EQ(fence3.duplicate(MX_RIGHT_SAME_RIGHTS, &temp_fence3), MX_OK);
  release_fence_signaler.AddCPUReleaseFence(std::move(temp_fence3));

  // Assert that none of the fences are signalled.
  ASSERT_FALSE(IsFenceSignalled(fence1));
  ASSERT_FALSE(IsFenceSignalled(fence2));

  // Mark the sequence numbers so far as finished. (Do it out of order for fun).
  sequencer.CommandBufferFinished(seq_num2);
  sequencer.CommandBufferFinished(seq_num1);

  ASSERT_TRUE(IsFenceSignalled(fence1));
  ASSERT_TRUE(IsFenceSignalled(fence2));
  ASSERT_FALSE(IsFenceSignalled(fence3));

  sequencer.CommandBufferFinished(seq_num3);

  ASSERT_TRUE(IsFenceSignalled(fence1));
  ASSERT_TRUE(IsFenceSignalled(fence2));
  ASSERT_TRUE(IsFenceSignalled(fence3));
}

}  // namespace test
}  // namespace scene
}  // namespace mozart
