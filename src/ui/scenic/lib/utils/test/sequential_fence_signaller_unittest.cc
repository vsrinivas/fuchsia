// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/utils/sequential_fence_signaller.h"

#include "gtest/gtest.h"
#include "src/ui/scenic/lib/utils/test/util.h"

namespace utils {
namespace test {

static zx::event CreateNewFence() {
  zx::event fence;
  EXPECT_EQ(ZX_OK, zx::event::create(0, &fence));
  return fence;
}

TEST(SequentialFenceSignallerTest, LowerSequenceNumber_ShouldNotSignalFence) {
  SequentialFenceSignaller signaller;

  zx::event fence = CreateNewFence();
  EXPECT_FALSE(IsEventSignalled(fence, ZX_EVENT_SIGNALED));

  signaller.AddFence(CopyEvent(fence), 2);

  signaller.SignalFencesUpToAndIncluding(1);
  EXPECT_FALSE(IsEventSignalled(fence, ZX_EVENT_SIGNALED));
}

TEST(SequentialFenceSignallerTest, SameSequenceNumber_ShouldSignalFence) {
  SequentialFenceSignaller signaller;

  zx::event fence = CreateNewFence();
  signaller.AddFence(CopyEvent(fence), 2);

  signaller.SignalFencesUpToAndIncluding(2);
  EXPECT_TRUE(IsEventSignalled(fence, ZX_EVENT_SIGNALED));
}

TEST(SequentialFenceSignallerTest, HigherSequenceNumber_ShouldSignalFence) {
  SequentialFenceSignaller signaller;

  zx::event fence = CreateNewFence();
  signaller.AddFence(CopyEvent(fence), 2);

  signaller.SignalFencesUpToAndIncluding(3);
  EXPECT_TRUE(IsEventSignalled(fence, ZX_EVENT_SIGNALED));
}

TEST(SequentialFenceSignallerTest, IfMultiple_ShouldOnlySignalFencesUpToAndIncludingSequence) {
  SequentialFenceSignaller signaller;

  zx::event fence1 = CreateNewFence();
  signaller.AddFence(CopyEvent(fence1), 1);

  zx::event fence2 = CreateNewFence();
  signaller.AddFence(CopyEvent(fence2), 2);

  zx::event fence3 = CreateNewFence();
  signaller.AddFence(CopyEvent(fence3), 3);

  signaller.SignalFencesUpToAndIncluding(2);
  EXPECT_TRUE(IsEventSignalled(fence1, ZX_EVENT_SIGNALED));
  EXPECT_TRUE(IsEventSignalled(fence2, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(IsEventSignalled(fence3, ZX_EVENT_SIGNALED));

  signaller.SignalFencesUpToAndIncluding(3);
  EXPECT_TRUE(IsEventSignalled(fence3, ZX_EVENT_SIGNALED));
}

TEST(SequentialFenceSignallerTest, OldSequenceNumber_ShouldSignalImmediately) {
  SequentialFenceSignaller signaller;

  signaller.SignalFencesUpToAndIncluding(2);

  zx::event fence = CreateNewFence();
  signaller.AddFence(CopyEvent(fence), 1);

  EXPECT_TRUE(IsEventSignalled(fence, ZX_EVENT_SIGNALED));
}

TEST(SequentialFenceSignallerTest, OutOfOrderAdds_ShouldStillSignalCorrectly) {
  SequentialFenceSignaller signaller;

  // Add out of sequence-order.
  zx::event fence1 = CreateNewFence();
  signaller.AddFence(CopyEvent(fence1), 2);

  zx::event fence2 = CreateNewFence();
  signaller.AddFence(CopyEvent(fence2), 1);

  signaller.SignalFencesUpToAndIncluding(1);
  EXPECT_FALSE(IsEventSignalled(fence1, ZX_EVENT_SIGNALED));
  EXPECT_TRUE(IsEventSignalled(fence2, ZX_EVENT_SIGNALED));

  signaller.SignalFencesUpToAndIncluding(2);
  EXPECT_TRUE(IsEventSignalled(fence1, ZX_EVENT_SIGNALED));
}

TEST(SequentialFenceSignallerTest, OutOfOrderSignalling_ShouldStillSignalCorrectly) {
  SequentialFenceSignaller signaller;

  zx::event fence1 = CreateNewFence();
  signaller.AddFence(CopyEvent(fence1), 1);
  zx::event fence2 = CreateNewFence();
  signaller.AddFence(CopyEvent(fence2), 2);
  zx::event fence3 = CreateNewFence();
  signaller.AddFence(CopyEvent(fence3), 3);

  // Signal out of order.
  signaller.SignalFencesUpToAndIncluding(2);
  signaller.SignalFencesUpToAndIncluding(1);
  signaller.SignalFencesUpToAndIncluding(3);
  EXPECT_TRUE(IsEventSignalled(fence1, ZX_EVENT_SIGNALED));
  EXPECT_TRUE(IsEventSignalled(fence2, ZX_EVENT_SIGNALED));
  EXPECT_TRUE(IsEventSignalled(fence3, ZX_EVENT_SIGNALED));
}

}  // namespace test
}  // namespace utils
