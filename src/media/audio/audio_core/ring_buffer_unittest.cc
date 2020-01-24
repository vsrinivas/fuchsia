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

class RingBufferTest : public testing::Test {
 protected:
  void SetUp() override {
    const auto& format = kDefaultFormat;
    auto timeline_function = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
        0, zx::time(0).get(), format.frames_per_second() * format.bytes_per_frame(),
        zx::sec(1).to_nsecs()));
    ring_buffer_ =
        RingBuffer::Allocate(format, std::move(timeline_function), kRingBufferFrameCount, true);
    ASSERT_TRUE(ring_buffer());
  }

  RingBuffer* ring_buffer() const { return ring_buffer_.get(); }

 private:
  std::shared_ptr<RingBuffer> ring_buffer_;
};

TEST_F(RingBufferTest, ReadEmptyRing) {
  auto buffer = ring_buffer()->LockBuffer(zx::time(0), 0, 1);
  ASSERT_FALSE(buffer);
}

TEST_F(RingBufferTest, ReadFullyExpiredBuffer) {
  // After 20ms, the ring will have been filled twice. If we request the first 480 frames then we
  // should get no buffer returned since all those frames are now unavailable.
  auto buffer = ring_buffer()->LockBuffer(zx::time(0) + zx::msec(20), 0, 480);
  ASSERT_FALSE(buffer);
}

TEST_F(RingBufferTest, ReadNotYetAvailableBuffer) {
  // After 10ms, 480 frames will have been produced (0-479). The 480th frame is not yet available
  // so we should get no buffer.
  auto buffer = ring_buffer()->LockBuffer(zx::time(0) + zx::msec(10), 480, 1);
  ASSERT_FALSE(buffer);
}

TEST_F(RingBufferTest, ReadFullyAvailableRegion) {
  // After 1ms we expect 48 frames to be available to read at the start of the buffer.
  auto buffer = ring_buffer()->LockBuffer(zx::time(0) + zx::msec(1), 0, 48);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), 0u);
  EXPECT_EQ(buffer->length().Floor(), 48u);
}

TEST_F(RingBufferTest, ReadPartialRegion) {
  // After 1ms we expect 48 frames to be available to read at the start of the buffer. If we ask for
  // 96 we should get a buffer that contains only the 48 available frames.
  auto buffer = ring_buffer()->LockBuffer(zx::time(0) + zx::msec(1), 0, 96);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), 0u);
  EXPECT_EQ(buffer->length().Floor(), 48u);
}

TEST_F(RingBufferTest, SkipExpiredFrames) {
  // At 11ms we'll have written the entire ring + 48 more samples, so the first 48 frames are lost.
  // Test that the returned buffer correctly skips those 48 frames.
  auto buffer = ring_buffer()->LockBuffer(zx::time(0) + zx::msec(11), 0, 96);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), 48u);
  EXPECT_EQ(buffer->length().Floor(), 48u);
}

TEST_F(RingBufferTest, TruncateBufferAtEndOfTheRing) {
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

}  // namespace
}  // namespace media::audio
