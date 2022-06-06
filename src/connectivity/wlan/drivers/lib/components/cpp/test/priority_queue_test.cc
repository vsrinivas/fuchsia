// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <wlan/drivers/components/frame.h>
#include <wlan/drivers/components/frame_container.h>
#include <wlan/drivers/components/frame_storage.h>
#include <wlan/drivers/components/priority_queue.h>
#include <zxtest/zxtest.h>

namespace {

using wlan::drivers::components::Frame;
using wlan::drivers::components::FrameContainer;
using wlan::drivers::components::FrameStorage;
using wlan::drivers::components::PriorityQueue;

constexpr size_t kQueueDepth = 2048;
constexpr uint8_t kAllPrioritiesAllowed = 0xFF;

uint8_t data[256] = {};
Frame CreateTestFrame(uint8_t priority = 0, FrameStorage* storage = nullptr) {
  constexpr uint8_t kVmoId = 13;
  constexpr size_t kVmoOffset = 0x0c00;
  constexpr uint8_t kPortId = 7;
  static uint16_t bufferId = 1;

  Frame frame(storage, kVmoId, kVmoOffset, bufferId++, data, sizeof(data), kPortId);
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

  FrameContainer frames;
  queue.pop(1, kAllPrioritiesAllowed, &frames);
  ASSERT_EQ(frames.size(), 1u);
  // Even though it was queued last we expect the highest priority frame to be popped first, after
  // all that's what a priority queue does.
  EXPECT_EQ(frames.front().Priority(), 2u);
  EXPECT_EQ(queue.size(), 2u);
  EXPECT_FALSE(queue.empty());

  frames.clear();
  queue.pop(1, kAllPrioritiesAllowed, &frames);
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames.front().Priority(), 1u);
  EXPECT_EQ(queue.size(), 1u);
  EXPECT_FALSE(queue.empty());

  frames.clear();
  queue.pop(1, kAllPrioritiesAllowed, &frames);
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames.front().Priority(), 0u);
  EXPECT_EQ(queue.size(), 0u);
  EXPECT_TRUE(queue.empty());

  frames.clear();
  queue.pop(1, kAllPrioritiesAllowed, &frames);
  EXPECT_TRUE(frames.empty());
}

TEST(PriorityQueueTest, PopMultiple) {
  PriorityQueue queue(kQueueDepth);

  queue.push(CreateTestFrame(3u));
  queue.push(CreateTestFrame(1u));
  queue.push(CreateTestFrame(7u));

  FrameContainer frames;
  queue.pop(3u, kAllPrioritiesAllowed, &frames);
  ASSERT_EQ(frames.size(), 3u);
  EXPECT_EQ(frames[0].Priority(), 7u);
  EXPECT_EQ(frames[1].Priority(), 3u);
  EXPECT_EQ(frames[2].Priority(), 1u);

  queue.push(CreateTestFrame(2u));
  queue.push(CreateTestFrame(4u));
  ASSERT_EQ(queue.size(), 2u);

  // Request more than available, should get as many as possible
  frames.clear();
  queue.pop(3u, kAllPrioritiesAllowed, &frames);
  ASSERT_EQ(frames.size(), 2u);
  EXPECT_EQ(frames[0].Priority(), 4u);
  EXPECT_EQ(frames[1].Priority(), 2u);
  EXPECT_TRUE(queue.empty());
}

TEST(PriorityQueueTest, FailedPopPreservesFrame) {
  wlan::drivers::components::FrameStorage storage;
  constexpr size_t kQueueCapacity = 1;
  PriorityQueue queue(kQueueCapacity);

  {
    FrameContainer frames;
    {
      std::lock_guard lock(storage);
      storage.Store(CreateTestFrame(0, &storage));
      storage.Store(CreateTestFrame(0, &storage));
      storage.Store(CreateTestFrame(0, &storage));
      frames = storage.Acquire(2u);
    }
    // We should have acquired two frames.
    ASSERT_EQ(2u, frames.size());

    {
      // One frame should remain
      std::lock_guard lock(storage);
      EXPECT_EQ(1u, storage.size());
    }
    // First push succeeds
    EXPECT_TRUE(queue.push(std::move(frames[0])));
    // But second one does not
    EXPECT_FALSE(queue.push(std::move(frames[1])));
    // At this point we let FrameContainer destruct.
  }
  // Both frames in the FrameContainer should now be destructed but the first frame that was
  // successfully pushed should not be returned to storage, it's still alive in the queue. Moving
  // from it should have cleared the storage pointer so that our reference to it doesn't have
  // storage anymore. The second frame however was not placed on the queue, it's important that it
  // retains its storage pointer and at this point it should have been returned to storage as part
  // of its destruction.

  std::lock_guard lock(storage);
  // Since the un-pushed frame should have been returned we should have 2 frames in storage and one
  // in the queue.
  EXPECT_EQ(1u, queue.size());
  EXPECT_EQ(2u, storage.size());
}

TEST(PriorityQueueTest, EvictByPriority) {
  constexpr size_t kQueueCapacity = 3;
  PriorityQueue queue(kQueueCapacity);

  // Create a few test frames with varying priorities and store their buffer IDs so we can verify
  // the ordering later.
  Frame frame5 = CreateTestFrame(4u);
  Frame frame6 = CreateTestFrame(3u);
  Frame frame7 = CreateTestFrame(1u);
  Frame frame8 = CreateTestFrame(5u);
  Frame frame9 = CreateTestFrame(0u);
  Frame frame10 = CreateTestFrame(2u);
  // Store the buffer IDs before we move the frames.
  uint16_t buffer_id5 = frame5.BufferId();
  uint16_t buffer_id6 = frame6.BufferId();
  uint16_t buffer_id7 = frame7.BufferId();
  uint16_t buffer_id8 = frame8.BufferId();
  uint16_t buffer_id9 = frame9.BufferId();
  uint16_t buffer_id10 = frame10.BufferId();

  queue.push(std::move(frame5));
  queue.push(std::move(frame6));
  queue.push(std::move(frame7));

  // Fails for lower priorities, succeeds on same or higher priority, lower priority frames evicted.
  // Ensure that the correct frame is returned in evicted.
  std::unique_ptr<Frame> evicted;
  ASSERT_FALSE(queue.push(std::move(frame9), &evicted));
  ASSERT_NOT_NULL(evicted.get());
  ASSERT_EQ(buffer_id9, evicted->BufferId());
  evicted.reset();
  ASSERT_TRUE(queue.push(std::move(frame10), &evicted));
  ASSERT_NOT_NULL(evicted.get());
  // This push succeeded but one frame was evicted, it should have been the lowest priority frame.
  ASSERT_EQ(buffer_id7, evicted->BufferId());
  ASSERT_TRUE(queue.push(std::move(frame8), &evicted));
  ASSERT_NOT_NULL(evicted.get());
  // This push succeeded but one frame was evicted, it should have been the lowest priority frame.
  ASSERT_EQ(buffer_id10, evicted->BufferId());
  // Queue should still be full afterwards, only one frame should have been evicted.
  ASSERT_EQ(queue.size(), kQueueCapacity);

  // Clear queue by popping everything
  FrameContainer frames;
  queue.pop(queue.size(), kAllPrioritiesAllowed, &frames);

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
  FrameContainer frames;
  queue.pop(queue.size(), kAllPrioritiesAllowed, &frames);

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
    FrameContainer frames;
    queue.pop(kPopsPerLoop, kAllPrioritiesAllowed, &frames);
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
  FrameContainer frames;
  queue_.pop(original_size, 0b0100'0000, &frames);
  EXPECT_EQ(frames.size(), priority_counts_[6]);
  EXPECT_EQ(queue_.size(), original_size - priority_counts_[6]);
  for (auto& frame : frames) {
    EXPECT_EQ(frame.Priority(), 6u);
  }

  // Pop as many frames as we can with multiple priorities
  original_size = queue_.size();
  frames.clear();
  queue_.pop(original_size, 0b0000'1001, &frames);
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
  frames.clear();
  queue_.pop(priority_counts_[7] - 1, 0b1011'0000, &frames);
  ASSERT_EQ(frames.size(), priority_counts_[7] - 1);
  for (auto& frame : frames) {
    EXPECT_EQ(frame.Priority(), 7u);
  }
}

TEST_F(PopulatedQueueTestFixture, PopAppends) {
  FrameContainer frames;

  const size_t first_pop_size = queue_.size() / 2;
  ASSERT_GT(first_pop_size, 0);
  queue_.pop(first_pop_size, kAllPrioritiesAllowed, &frames);
  EXPECT_EQ(first_pop_size, frames.size());

  const uint16_t first_buffer_id = frames.front().BufferId();

  // Pop again and verify that frames were appended and that the frame container was not cleared.
  const size_t second_pop_size = first_pop_size / 2;
  ASSERT_GT(second_pop_size, 0);
  queue_.pop(second_pop_size, kAllPrioritiesAllowed, &frames);
  EXPECT_EQ(first_pop_size + second_pop_size, frames.size());

  // Verify that the first frame is still at the beginning, ensuring that pop appends, not prepends.
  EXPECT_EQ(first_buffer_id, frames.front().BufferId());
}

TEST(PriorityQueueTest, PopIfMatchesPriority) {
  PriorityQueue queue(kQueueDepth);
  constexpr uint8_t kPopWithPriority = 3;

  queue.push(CreateTestFrame(kPopWithPriority));
  queue.push(CreateTestFrame(1u));
  queue.push(CreateTestFrame(kPopWithPriority));
  queue.push(CreateTestFrame(kPopWithPriority));
  queue.push(CreateTestFrame(4u));
  queue.push(CreateTestFrame(2u));
  queue.push(CreateTestFrame(kPopWithPriority));
  queue.push(CreateTestFrame(5u));
  queue.push(CreateTestFrame(kPopWithPriority));

  const size_t queue_size = queue.size();

  FrameContainer frames;
  queue.pop_if([](const Frame& frame) { return frame.Priority() == kPopWithPriority; }, &frames);
  ASSERT_EQ(frames.size(), 5u);
  EXPECT_EQ(queue.size(), queue_size - frames.size());

  for (const auto& frame : frames) {
    EXPECT_EQ(frame.Priority(), kPopWithPriority);
  }

  // Now attempt to pop frames with the same priority that we popped using pop_if, there should be
  // none.
  frames.clear();
  queue.pop(100u, 1 << kPopWithPriority, &frames);
  ASSERT_TRUE(frames.empty());
}

TEST(PriorityQueueTest, PopIfEveryOtherFrame) {
  PriorityQueue queue(kQueueDepth);

  // Insert frames with varying priority, insert them in priority order so that it will be easier to
  // reason about how they will be popped. Higher priorities will be popped first.
  queue.push(CreateTestFrame(7u));
  queue.push(CreateTestFrame(7u));
  queue.push(CreateTestFrame(6u));
  queue.push(CreateTestFrame(5u));
  queue.push(CreateTestFrame(5u));
  queue.push(CreateTestFrame(4u));
  queue.push(CreateTestFrame(3u));
  queue.push(CreateTestFrame(2u));
  queue.push(CreateTestFrame(1u));

  const size_t queue_size = queue.size();

  size_t counter = 0;
  FrameContainer frames;
  queue.pop_if([&](const Frame& frame) { return counter++ % 2 == 0; }, &frames);
  ASSERT_EQ(counter, queue_size);
  ASSERT_EQ(frames.size(), 5u);
  EXPECT_EQ(queue.size(), queue_size - frames.size());

  // Based on the insertions above we should get every other frame starting from the first.
  EXPECT_EQ(frames[0].Priority(), 7u);
  EXPECT_EQ(frames[1].Priority(), 6u);
  EXPECT_EQ(frames[2].Priority(), 5u);
  EXPECT_EQ(frames[3].Priority(), 3u);
  EXPECT_EQ(frames[4].Priority(), 1u);
}

TEST(PriorityQueueTest, PopIfEverything) {
  PriorityQueue queue(kQueueDepth);

  // Insert frames with varying priority, insert them in priority order so that it will be easier to
  // reason about how they will be popped. Higher priorities will be popped first.

  queue.push(CreateTestFrame(7u));
  queue.push(CreateTestFrame(7u));
  queue.push(CreateTestFrame(6u));
  queue.push(CreateTestFrame(5u));
  queue.push(CreateTestFrame(5u));

  const size_t queue_size = queue.size();

  FrameContainer frames;
  queue.pop_if([](const Frame& frame) { return true; }, &frames);

  ASSERT_EQ(queue_size, frames.size());
  EXPECT_TRUE(queue.empty());

  // Based on the insertions above we should get every frame in order of priority.
  EXPECT_EQ(frames[0].Priority(), 7u);
  EXPECT_EQ(frames[1].Priority(), 7u);
  EXPECT_EQ(frames[2].Priority(), 6u);
  EXPECT_EQ(frames[3].Priority(), 5u);
  EXPECT_EQ(frames[4].Priority(), 5u);
}

TEST_F(PopulatedQueueTestFixture, PopIfAppends) {
  FrameContainer frames;

  const size_t first_pop_size = queue_.size() / 2;
  ASSERT_GT(first_pop_size, 0);
  size_t counter = 0;
  queue_.pop_if([&](const Frame&) { return counter++ < first_pop_size; }, &frames);
  EXPECT_EQ(first_pop_size, frames.size());

  const uint16_t first_buffer_id = frames.front().BufferId();

  // Pop again and verify that frames were appended and that the frame container was not cleared.
  const size_t second_pop_size = first_pop_size / 2;
  ASSERT_GT(second_pop_size, 0);
  counter = 0;
  queue_.pop_if([&](const Frame&) { return counter++ < second_pop_size; }, &frames);
  EXPECT_EQ(first_pop_size + second_pop_size, frames.size());

  // Verify that the first frame is still at the beginning, ensuring that pop_if appends, not
  // prepends.
  EXPECT_EQ(first_buffer_id, frames.front().BufferId());
}

}  // namespace
