// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <wlan/drivers/components/frame.h>
#include <wlan/drivers/components/priority_queue.h>
#include <zxtest/zxtest.h>

namespace {

using wlan::drivers::components::Frame;
using wlan::drivers::components::PriorityQueue;

constexpr size_t kQueueDepth = 2048;
constexpr uint8_t kAllPrioritiesAllowed = 0xFF;

uint8_t data[256] = {};
Frame CreateTestFrame(uint8_t priority = 0) {
  constexpr uint8_t kVmoId = 13;
  constexpr size_t kVmoOffset = 0x0c00;
  constexpr uint16_t kBufferId = 123;
  constexpr uint8_t kPortId = 7;

  Frame frame(nullptr, kVmoId, kVmoOffset, kBufferId, data, sizeof(data), kPortId);
  frame.SetPriority(priority);
  return frame;
}

TEST(PriorityQueueTest, Constructible) {
  PriorityQueue queue(kQueueDepth);
  EXPECT_EQ(queue.capacity(), kQueueDepth);
}

TEST(PriorityQueueTest, Push) {
  PriorityQueue queue(kQueueDepth);

  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0u);
  EXPECT_EQ(queue.size(kAllPrioritiesAllowed), 0u);
  queue.push(CreateTestFrame());
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(queue.size(), 1u);
  EXPECT_EQ(queue.size(kAllPrioritiesAllowed), 1u);
  queue.push(CreateTestFrame());
  EXPECT_EQ(queue.size(), 2u);
  EXPECT_EQ(queue.size(kAllPrioritiesAllowed), 2u);
  queue.push(CreateTestFrame());
  EXPECT_EQ(queue.size(), 3u);
  EXPECT_EQ(queue.size(kAllPrioritiesAllowed), 3u);
}

TEST(PriorityQueueTest, Pop) {
  PriorityQueue queue(kQueueDepth);

  queue.push(CreateTestFrame(0u));
  queue.push(CreateTestFrame(1u));
  queue.push(CreateTestFrame(2u));

  cpp20::span<Frame> frames = queue.pop(1, kAllPrioritiesAllowed);
  ASSERT_EQ(frames.size(), 1u);
  // Even though it was queued last we expect the highest priority frame to be popped first, after
  // all that's what a priority queue does.
  EXPECT_EQ(frames.front().Priority(), 2u);
  EXPECT_EQ(queue.size(), 2u);
  EXPECT_FALSE(queue.empty());

  frames = queue.pop(1, kAllPrioritiesAllowed);
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames.front().Priority(), 1u);
  EXPECT_EQ(queue.size(), 1u);
  EXPECT_FALSE(queue.empty());

  frames = queue.pop(1, kAllPrioritiesAllowed);
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames.front().Priority(), 0u);
  EXPECT_EQ(queue.size(), 0u);
  EXPECT_TRUE(queue.empty());

  frames = queue.pop(1, kAllPrioritiesAllowed);
  EXPECT_TRUE(frames.empty());
}

TEST(PriorityQueueTest, PopMultiple) {
  PriorityQueue queue(kQueueDepth);

  queue.push(CreateTestFrame(3u));
  queue.push(CreateTestFrame(1u));
  queue.push(CreateTestFrame(7u));

  cpp20::span<Frame> frames = queue.pop(3u, kAllPrioritiesAllowed);
  ASSERT_EQ(frames.size(), 3u);
  EXPECT_EQ(frames[0].Priority(), 7u);
  EXPECT_EQ(frames[1].Priority(), 3u);
  EXPECT_EQ(frames[2].Priority(), 1u);

  queue.push(CreateTestFrame(2u));
  queue.push(CreateTestFrame(4u));
  ASSERT_EQ(queue.size(), 2u);

  // Request more than available, should get as many as possible
  frames = queue.pop(3u, kAllPrioritiesAllowed);
  ASSERT_EQ(frames.size(), 2u);
  EXPECT_EQ(frames[0].Priority(), 4u);
  EXPECT_EQ(frames[1].Priority(), 2u);
  EXPECT_TRUE(queue.empty());
}

TEST(PriorityQueueTest, EvictByPriority) {
  constexpr size_t kQueueCapacity = 3;
  PriorityQueue queue(kQueueCapacity);

  // Create a few test frames with varying priorities and store their buffer IDs so we can verify
  // the ordering later.
  Frame frame5 = CreateTestFrame(3u);
  Frame frame6 = CreateTestFrame(2u);
  Frame frame7 = CreateTestFrame(1u);
  Frame frame8 = CreateTestFrame(4u);
  // Store the buffer IDs before we move the frames.
  uint16_t buffer_id5 = frame5.BufferId();
  uint16_t buffer_id6 = frame6.BufferId();
  uint16_t buffer_id8 = frame8.BufferId();

  queue.push(std::move(frame5));
  queue.push(std::move(frame6));
  queue.push(std::move(frame7));

  // Fails for lower priorities, succeeds on same or higher priority, lower priority frames evicted.
  ASSERT_FALSE(queue.push(CreateTestFrame(0u)));
  ASSERT_TRUE(queue.push(CreateTestFrame(2u)));
  ASSERT_TRUE(queue.push(std::move(frame8)));
  // Queue should still be full afterwards, only one frame should have been evicted.
  ASSERT_EQ(queue.size(), kQueueCapacity);

  // Clear queue by popping everything
  cpp20::span<Frame> frames = queue.pop(queue.size(), kAllPrioritiesAllowed);

  // Assert that the ordering of the popped frames is correct and that the correct frame was
  // evicted.
  ASSERT_EQ(buffer_id8, frames[0].BufferId());
  ASSERT_EQ(buffer_id5, frames[1].BufferId());
  ASSERT_EQ(buffer_id6, frames[2].BufferId());
}

TEST(PriorityQueueTest, EvictOldestFirst) {
  constexpr size_t kQueueCapacity = 3;
  PriorityQueue queue(kQueueCapacity);

  // Create a few test frames, the last frame with a higher priority so that one of the first frames
  // will get evicted.
  Frame frame1 = CreateTestFrame(1u);
  Frame frame2 = CreateTestFrame(1u);
  Frame frame3 = CreateTestFrame(1u);
  Frame frame4 = CreateTestFrame(2u);
  // Store the buffer IDs before we move the frames.
  uint16_t buffer_id2 = frame2.BufferId();
  uint16_t buffer_id3 = frame3.BufferId();
  uint16_t buffer_id4 = frame4.BufferId();

  queue.push(std::move(frame1));
  queue.push(std::move(frame2));
  queue.push(std::move(frame3));

  // Fails for lower and same priority, succeeds on higher priority, lower priority frames evicted.
  ASSERT_FALSE(queue.push(CreateTestFrame(0u)));
  ASSERT_FALSE(queue.push(CreateTestFrame(1u)));
  ASSERT_TRUE(queue.push(std::move(frame4)));
  // Queue should still be full afterwards, only one frame should have been evicted.
  ASSERT_EQ(queue.size(), kQueueCapacity);

  // Clear queue by popping everything
  cpp20::span<Frame> frames = queue.pop(queue.size(), kAllPrioritiesAllowed);

  // Assert that the ordering of the popped frames is correct and that the correct frame was
  // evicted.
  ASSERT_EQ(buffer_id4, frames[0].BufferId());
  ASSERT_EQ(buffer_id2, frames[1].BufferId());
  ASSERT_EQ(buffer_id3, frames[2].BufferId());
}

TEST(PriorityQueueTest, PushInvalidPriority) {
  PriorityQueue queue(kQueueDepth);

  EXPECT_TRUE(queue.push(CreateTestFrame(7u)));
  EXPECT_FALSE(queue.push(CreateTestFrame(8u)));
  EXPECT_FALSE(queue.push(CreateTestFrame(9u)));
  EXPECT_FALSE(queue.push(CreateTestFrame(13u)));
  EXPECT_FALSE(queue.push(CreateTestFrame(255u)));
}

TEST(PriorityQueueTest, SaturationBalance) {
  // Set up a situation where two streams are pushing onto the queue with different priorities. The
  // total amount of pushes exceeds the number of pops, saturating the queue. Ensure that the higher
  // priority stream gets to transmit and that the lower priority stream does not get starved.
  constexpr uint32_t kPopsPerLoop = 32;
  constexpr uint32_t kPushesStreamOnePerLoop = 24;
  constexpr uint32_t kPushesStreamTwoPerLoop = 16;
  constexpr uint8_t kStreamOnePriority = 2;
  constexpr uint8_t kStreamTwoPriority = 0;

  constexpr uint32_t kQueueDepth = 512;

  PriorityQueue queue(kQueueDepth);

  size_t stream_one_frames = 0;
  size_t stream_two_frames = 0;

  constexpr size_t kIterations = 10'000;
  for (size_t i = 0; i < kIterations; ++i) {
    for (size_t streamOne = 0; streamOne < kPushesStreamOnePerLoop; ++streamOne) {
      queue.push(CreateTestFrame(kStreamOnePriority));
    }
    for (size_t streamTwo = 0; streamTwo < kPushesStreamTwoPerLoop; ++streamTwo) {
      queue.push(CreateTestFrame(kStreamTwoPriority));
    }
    cpp20::span<Frame> frames = queue.pop(kPopsPerLoop, kAllPrioritiesAllowed);
    for (const auto& frame : frames) {
      if (frame.Priority() == kStreamOnePriority) {
        ++stream_one_frames;
      } else if (frame.Priority() == kStreamTwoPriority) {
        ++stream_two_frames;
      } else {
        // Cannot be allowed to happen
        ASSERT_FALSE(true);
      }
    }
  }

  constexpr size_t kTotalPops = kIterations * kPopsPerLoop;
  // Stream one should have been able to send all its frames
  constexpr size_t kMaxStreamOnePops = kIterations * kPushesStreamOnePerLoop;
  // Stream two should have gotten whatever is left
  constexpr size_t kMaxStreamTwoPops = kTotalPops - kMaxStreamOnePops;

  ASSERT_EQ(stream_one_frames, kMaxStreamOnePops);
  ASSERT_EQ(stream_two_frames, kMaxStreamTwoPops);

  // Make sure we sent less than we actually wanted to on stream two
  ASSERT_LT(stream_two_frames, kIterations * kPushesStreamTwoPerLoop);
}

struct PopulatedQueueTestFixture : public ::zxtest::Test {
  PopulatedQueueTestFixture() : queue_(kQueueDepth) {}
  void SetUp() override {
    priority_counts_.resize(kQueueDepth);
    // Add frames for each priority
    for (uint8_t priority = 0; priority < 8; ++priority) {
      // Add a varying number of frames for each priority so that we can detect if different
      // priority levels get mixed up.
      priority_counts_[priority] = 2 + priority;
      for (size_t i = 0; i < priority_counts_[priority]; ++i) {
        queue_.push(CreateTestFrame(priority));
      }
    }
    EXPECT_EQ(queue_.size(kAllPrioritiesAllowed), 44u);
  }
  PriorityQueue queue_;
  std::vector<size_t> priority_counts_;
};

TEST_F(PopulatedQueueTestFixture, SizeWithSelectPriorities) {
  ASSERT_EQ(queue_.size(), 44u);

  // Size for each individual priority
  for (uint8_t priority = 0; priority < 8u; ++priority) {
    const uint8_t allowed_priorities = static_cast<uint8_t>(1 << priority);
    EXPECT_EQ(queue_.size(allowed_priorities), priority_counts_[priority]);
  }

  // Size for combinations of allowed priorities
  EXPECT_EQ(queue_.size(0b0010'0100), priority_counts_[2] + priority_counts_[5]);
  EXPECT_EQ(queue_.size(0b1100'0010),
            priority_counts_[7] + priority_counts_[6] + priority_counts_[1]);
}

TEST_F(PopulatedQueueTestFixture, PopWithLimitedPriorities) {
  ASSERT_EQ(queue_.size(), 44u);

  // Pop as many frames as we can with only a single priority
  size_t original_size = queue_.size();
  cpp20::span<Frame> frames = queue_.pop(original_size, 0b0100'0000);
  EXPECT_EQ(frames.size(), priority_counts_[6]);
  EXPECT_EQ(queue_.size(), original_size - priority_counts_[6]);
  for (auto& frame : frames) {
    EXPECT_EQ(frame.Priority(), 6u);
  }

  // Pop as many frames as we can with multiple priorities
  original_size = queue_.size();
  frames = queue_.pop(original_size, 0b0000'1001);
  const size_t expected_num_frames = priority_counts_[0] + priority_counts_[3];
  ASSERT_EQ(frames.size(), expected_num_frames);
  EXPECT_EQ(queue_.size(), original_size - expected_num_frames);
  for (size_t i = 0; i < priority_counts_[3]; ++i) {
    EXPECT_EQ(frames[i].Priority(), 3u);
  }

  // The first frames should all have priority 3
  for (size_t i = 0; i < priority_counts_[3]; ++i) {
    EXPECT_EQ(frames[i].Priority(), 3u);
  }
  // The remaining frames should have priority 0
  for (size_t i = priority_counts_[3]; i < expected_num_frames; ++i) {
    EXPECT_EQ(frames[i].Priority(), 0u);
  }

  // Pop fewer than available frame for one priority while allowing multiple priorities, only frames
  // with the highest priority should be popped.
  ASSERT_GT(priority_counts_[7], 0u);
  ASSERT_EQ(priority_counts_[7], queue_.size(0b1000'0000));
  frames = queue_.pop(priority_counts_[7] - 1, 0b1011'0000);
  ASSERT_EQ(frames.size(), priority_counts_[7] - 1);
  for (auto& frame : frames) {
    EXPECT_EQ(frame.Priority(), 7u);
  }
}

}  // namespace
