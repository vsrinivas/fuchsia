// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/ring_buffer.h"

#include <gtest/gtest.h>

namespace media::audio {
namespace {

const Format kDefaultFormat = Format(fuchsia::media::AudioStreamType{
    .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
    .channels = 2,
    .frames_per_second = 48000,
});

// 10ms @ 48khz
const uint32_t kRingBufferFrameCount = 480;

template <RingBuffer::Endpoint Endpoint>
class RingBufferTest : public testing::Test {
 protected:
  void SetUp() override {
    const auto& format = kDefaultFormat;
    auto timeline_function = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
        0, zx::time(0).get(), FractionalFrames<int64_t>(format.frames_per_second()).raw_value(),
        zx::sec(1).to_nsecs()));
    ring_buffer_ =
        RingBuffer::Allocate(format, std::move(timeline_function), kRingBufferFrameCount, Endpoint);
    ASSERT_TRUE(ring_buffer());

    EXPECT_EQ(Endpoint, ring_buffer_->endpoint());
  }

  RingBuffer* ring_buffer() const { return ring_buffer_.get(); }

 private:
  std::shared_ptr<RingBuffer> ring_buffer_;
};

using InputRingBufferTest = RingBufferTest<RingBuffer::Endpoint::kReadable>;
using OutputRingBufferTest = RingBufferTest<RingBuffer::Endpoint::kWritable>;

TEST_F(InputRingBufferTest, ReadEmptyRing) {
  auto buffer = ring_buffer()->LockBuffer(zx::time(0), 0, 1);
  ASSERT_FALSE(buffer);
}

TEST_F(InputRingBufferTest, ReadFullyExpiredBuffer) {
  // After 20ms, the ring will have been filled twice. If we request the first 480 frames then we
  // should get no buffer returned since all those frames are now unavailable.
  auto buffer = ring_buffer()->LockBuffer(zx::time(0) + zx::msec(20), 0, kRingBufferFrameCount);
  ASSERT_FALSE(buffer);
}

TEST_F(InputRingBufferTest, ReadNotYetAvailableBuffer) {
  // After 10ms, 480 frames will have been produced (0-479). The 480th frame is not yet available
  // so we should get no buffer.
  auto buffer = ring_buffer()->LockBuffer(zx::time(0) + zx::msec(10), 480, 1);
  ASSERT_FALSE(buffer);
}

TEST_F(InputRingBufferTest, ReadFullyAvailableRegion) {
  // After 1ms we expect 48 frames to be available to read at the start of the buffer.
  auto buffer = ring_buffer()->LockBuffer(zx::time(0) + zx::msec(1), 0, 48);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), 0u);
  EXPECT_EQ(buffer->length().Floor(), 48u);
}

TEST_F(InputRingBufferTest, ReadPartialRegion) {
  // After 1ms we expect 48 frames to be available to read at the start of the buffer. If we ask for
  // 96 we should get a buffer that contains only the 48 available frames.
  auto buffer = ring_buffer()->LockBuffer(zx::time(0) + zx::msec(1), 0, 96);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), 0u);
  EXPECT_EQ(buffer->length().Floor(), 48u);
}

TEST_F(InputRingBufferTest, SkipExpiredFrames) {
  // At 11ms we'll have written the entire ring + 48 more samples, so the first 48 frames are lost.
  // Test that the returned buffer correctly skips those 48 frames.
  auto buffer = ring_buffer()->LockBuffer(zx::time(0) + zx::msec(11), 0, 96);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), 48u);
  EXPECT_EQ(buffer->length().Floor(), 48u);
}

TEST_F(InputRingBufferTest, TruncateBufferAtEndOfTheRing) {
  // After 11ms 528 frames will have been put into the ring. We try to read the last 96 frames,
  // which spans the last 48 frames in the ring and then the first 48 frames at the start of the
  // ring again. Test our buffer is truncated for the first 48 frames requested at the end of the
  // ring.
  auto buffer = ring_buffer()->LockBuffer(zx::time(0) + zx::msec(11), 432, 96);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), 432u);
  EXPECT_EQ(buffer->length().Floor(), 48u);

  // Now read that last 48 frames at the start of the ring again.
  buffer = ring_buffer()->LockBuffer(zx::time(0) + zx::msec(11), 480, 48);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), 480u);
  EXPECT_EQ(buffer->length().Floor(), 48u);
}

TEST_F(OutputRingBufferTest, ReadEmptyRing) {
  auto buffer = ring_buffer()->LockBuffer(zx::time(0), 0, 1);
  ASSERT_TRUE(buffer);
  ASSERT_EQ(0u, buffer->start().Floor());
  ASSERT_EQ(1u, buffer->length().Floor());
}

TEST_F(OutputRingBufferTest, ReadFullyExpiredBuffer) {
  // After 10ms the hardware will have already consumed the first kRingBufferFrameCount frames.
  // Attempting to get a buffer into that region should not be successful.
  auto buffer = ring_buffer()->LockBuffer(zx::time(0) + zx::msec(10), 0, kRingBufferFrameCount);
  ASSERT_FALSE(buffer);
}

TEST_F(OutputRingBufferTest, ReadNotYetAvailableBuffer) {
  // Trying to get a buffer more than |kRingBufferFrameCOunt| frames into the future will fail as it
  // would cause use to clobber frames not yet consumed by the hardware.
  auto buffer = ring_buffer()->LockBuffer(zx::time(0), kRingBufferFrameCount, 1);
  ASSERT_FALSE(buffer);
}

TEST_F(OutputRingBufferTest, ReadFullyAvailableRegion) {
  // At time 0, we should be able to get a full buffer into the ring.
  auto buffer = ring_buffer()->LockBuffer(zx::time(0), 0, kRingBufferFrameCount);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), 0u);
  EXPECT_EQ(buffer->length().Floor(), kRingBufferFrameCount);
}

TEST_F(OutputRingBufferTest, ReadPartialRegion) {
  // After 1ms we expect 48 frames to have been consumed, so we can only get a buffer into the final
  // 48 requested frames.
  auto buffer = ring_buffer()->LockBuffer(zx::time(0) + zx::msec(1), 0, 96);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), 48u);
  EXPECT_EQ(buffer->length().Floor(), 48u);
}

TEST_F(OutputRingBufferTest, TruncateBufferAtEndOfTheRing) {
  // After 9ms, the first 432 frames will have been consumed by hardware. Attempting to read the
  // next 96 frames will wrap around the ring, so we should only get the first 48 returned.
  auto buffer = ring_buffer()->LockBuffer(zx::time(0) + zx::msec(9), 432, 96);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), 432u);
  EXPECT_EQ(buffer->length().Floor(), 48u);

  // Now read that last 48 frames at the start of the ring again.
  buffer = ring_buffer()->LockBuffer(zx::time(0) + zx::msec(9), kRingBufferFrameCount, 48);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), kRingBufferFrameCount);
  EXPECT_EQ(buffer->length().Floor(), 48u);
}

}  // namespace
}  // namespace media::audio
