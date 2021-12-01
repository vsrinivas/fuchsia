// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <wlan/drivers/components/frame.h>
#include <wlan/drivers/components/frame_storage.h>
#include <zxtest/zxtest.h>

namespace {

using wlan::drivers::components::Frame;
using wlan::drivers::components::FrameStorage;

constexpr uint8_t kVmoId = 13;
constexpr size_t kVmoOffset = 0x0c00;
constexpr uint16_t kBufferId = 123;
constexpr uint8_t kPortId = 7;
constexpr uint8_t kPriority = 9;
uint8_t data[256] = {};

TEST(FrameTest, DefaultConstructible) { Frame frame; }

TEST(FrameTest, ParamConstructible) {
  Frame frame(nullptr, kVmoId, kVmoOffset, kBufferId, data, sizeof(data), kPortId);

  EXPECT_EQ(frame.VmoId(), kVmoId);
  EXPECT_EQ(frame.VmoOffset(), kVmoOffset);
  EXPECT_EQ(frame.BufferId(), kBufferId);
  EXPECT_EQ(frame.Data(), data);
  EXPECT_EQ(frame.Size(), sizeof(data));
  EXPECT_EQ(frame.PortId(), kPortId);
  EXPECT_EQ(frame.Headroom(), 0u);
}

TEST(FrameTest, MoveConstructible) {
  Frame frame(nullptr, kVmoId, kVmoOffset, kBufferId, data, sizeof(data), kPortId);

  Frame new_frame(std::move(frame));

  EXPECT_EQ(new_frame.VmoId(), kVmoId);
  EXPECT_EQ(new_frame.VmoOffset(), kVmoOffset);
  EXPECT_EQ(new_frame.BufferId(), kBufferId);
  EXPECT_EQ(new_frame.Data(), data);
  EXPECT_EQ(new_frame.Size(), sizeof(data));
  EXPECT_EQ(new_frame.PortId(), kPortId);
  EXPECT_EQ(new_frame.Headroom(), 0u);
}

TEST(FrameTest, MoveAssignable) {
  Frame frame(nullptr, kVmoId, kVmoOffset, kBufferId, data, sizeof(data), kPortId);

  Frame new_frame(nullptr, 0, 0, 0, nullptr, 0, 0);

  new_frame = std::move(frame);

  EXPECT_EQ(new_frame.VmoId(), kVmoId);
  EXPECT_EQ(new_frame.VmoOffset(), kVmoOffset);
  EXPECT_EQ(new_frame.BufferId(), kBufferId);
  EXPECT_EQ(new_frame.Data(), data);
  EXPECT_EQ(new_frame.Size(), sizeof(data));
  EXPECT_EQ(new_frame.PortId(), kPortId);
  EXPECT_EQ(new_frame.Headroom(), 0u);
}

TEST(FrameTest, SetParameters) {
  Frame frame;

  frame.SetSize(sizeof(data));
  frame.SetPriority(kPriority);
  frame.SetPortId(kPortId);

  EXPECT_EQ(frame.Size(), sizeof(data));
  EXPECT_EQ(frame.Priority(), kPriority);
  EXPECT_EQ(frame.PortId(), kPortId);
}

TEST(FrameTest, GrowAndShrink) {
  Frame frame(nullptr, kVmoId, kVmoOffset, kBufferId, data, sizeof(data), kPortId);

  constexpr uint32_t kHeadShrinkage = 42u;
  constexpr uint32_t kHeadGrowth = 17u;

  frame.ShrinkHead(kHeadShrinkage);
  EXPECT_EQ(frame.Headroom(), kHeadShrinkage);
  EXPECT_EQ(frame.Data(), data + kHeadShrinkage);
  EXPECT_EQ(frame.VmoOffset(), kVmoOffset + kHeadShrinkage);
  EXPECT_EQ(frame.Size(), sizeof(data) - kHeadShrinkage);

  frame.GrowHead(kHeadGrowth);
  EXPECT_EQ(frame.Headroom(), kHeadShrinkage - kHeadGrowth);
  EXPECT_EQ(frame.Data(), data + kHeadShrinkage - kHeadGrowth);
  EXPECT_EQ(frame.VmoOffset(), kVmoOffset + kHeadShrinkage - kHeadGrowth);
  EXPECT_EQ(frame.Size(), sizeof(data) - kHeadShrinkage + kHeadGrowth);

  constexpr uint32_t kTailShrinkage = 64u;
  constexpr uint32_t kTailGrowth = 9u;
  const uint32_t initial_size = frame.Size();

  frame.ShrinkTail(kTailShrinkage);
  EXPECT_EQ(frame.Size(), initial_size - kTailShrinkage);
  frame.GrowTail(kTailGrowth);
  EXPECT_EQ(frame.Size(), initial_size - kTailShrinkage + kTailGrowth);
}

TEST(FrameTest, AutomaticLifetime) {
  FrameStorage storage;
  Frame moved_frame;
  uint16_t frame1_buffer_id = 0;
  uint16_t frame2_buffer_id = 0;
  {
    Frame frame1, frame2;
    {
      std::lock_guard lock(storage);
      storage.Store(Frame(&storage, kVmoId, kVmoOffset, kBufferId, data, sizeof(data), kPortId));
      storage.Store(
          Frame(&storage, kVmoId, kVmoOffset, kBufferId + 1, data, sizeof(data), kPortId));
      frame1 = *storage.Acquire();
      frame2 = *storage.Acquire();
      ASSERT_TRUE(storage.empty());
    }
    frame1_buffer_id = frame1.BufferId();
    frame2_buffer_id = frame2.BufferId();

    // Modify the frame1's data pointer, headroom and size by shrinking its head. This should be
    // reverted once the frame is returned to storage.
    frame1.ShrinkHead(42u);
    // Modify frame2 as well and verify that it does not change when moved.
    frame2.ShrinkHead(13u);
    moved_frame = std::move(frame2);
    // frame1 goes out of scope and should be returned to storage
    // frame2 was moved from and should not be returned to storage
  }
  std::lock_guard lock(storage);
  // frame1 should have returned to storage and been reset, frame2 should now be in moved_frame
  ASSERT_EQ(storage.size(), 1u);
  EXPECT_EQ(storage.front().VmoId(), kVmoId);
  EXPECT_EQ(storage.front().VmoOffset(), kVmoOffset);
  EXPECT_EQ(storage.front().BufferId(), frame1_buffer_id);
  EXPECT_EQ(storage.front().Data(), data);
  EXPECT_EQ(storage.front().Size(), sizeof(data));
  EXPECT_EQ(storage.front().PortId(), kPortId);
  EXPECT_EQ(storage.front().Headroom(), 0u);

  EXPECT_EQ(moved_frame.VmoId(), kVmoId);
  EXPECT_EQ(moved_frame.VmoOffset(), kVmoOffset + 13u);
  EXPECT_EQ(moved_frame.BufferId(), frame2_buffer_id);
  EXPECT_EQ(moved_frame.Data(), data + 13);
  EXPECT_EQ(moved_frame.Size(), sizeof(data) - 13u);
  EXPECT_EQ(moved_frame.PortId(), kPortId);
  EXPECT_EQ(moved_frame.Headroom(), 13u);
}

TEST(FrameTest, ManualLifetime) {
  FrameStorage storage;
  Frame frame;
  {
    std::lock_guard lock(storage);
    storage.Store(Frame(&storage, kVmoId, kVmoOffset, kBufferId, data, sizeof(data), kPortId));
    frame = *storage.Acquire();
    ASSERT_TRUE(storage.empty());
  }

  // Modify the frame's data pointer, headroom and size by shrinking its head. This should be
  // reverted once the frame is returned to storage.
  frame.ShrinkHead(42u);

  // Return it.
  frame.ReturnToStorage();

  std::lock_guard lock(storage);
  ASSERT_FALSE(storage.empty());
  EXPECT_EQ(storage.front().VmoId(), kVmoId);
  EXPECT_EQ(storage.front().VmoOffset(), kVmoOffset);
  EXPECT_EQ(storage.front().BufferId(), kBufferId);
  EXPECT_EQ(storage.front().Data(), data);
  EXPECT_EQ(storage.front().Size(), sizeof(data));
  EXPECT_EQ(storage.front().PortId(), kPortId);
  EXPECT_EQ(storage.front().Headroom(), 0u);
}

TEST(FrameTest, ReleaseFromStorage) {
  constexpr uint32_t kShrinkage = 42u;
  FrameStorage storage;
  {
    Frame frame;
    {
      std::lock_guard lock(storage);
      storage.Store(Frame(&storage, kVmoId, kVmoOffset, kBufferId, data, sizeof(data), kPortId));
      frame = *storage.Acquire();
      ASSERT_TRUE(storage.empty());
    }

    // Modify the frame's data pointer, headroom and size by shrinking its head. This should be
    // reverted once the frame is returned to storage.
    frame.ShrinkHead(kShrinkage);

    frame.ReleaseFromStorage();
    // Frame goes out of scope, it should not be returned to storage because it was released.
  }
  std::lock_guard lock(storage);
  ASSERT_TRUE(storage.empty());
}

}  // namespace
