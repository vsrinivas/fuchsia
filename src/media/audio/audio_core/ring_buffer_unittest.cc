// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/ring_buffer.h"

#include <lib/zx/clock.h>

#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/clock/testing/clock_test.h"

namespace media::audio {
namespace {

const Format kDefaultFormat =
    Format::Create(fuchsia::media::AudioStreamType{
                       .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                       .channels = 2,
                       .frames_per_second = 48000,
                   })
        .take_value();

// 10ms @ 48khz
const uint32_t kRingBufferFrameCount = 480;

class InputRingBufferTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto& format = kDefaultFormat;
    auto timeline_function = fbl::MakeRefCounted<VersionedTimelineFunction>(
        TimelineFunction(0, zx::time(0).get(), Fixed(format.frames_per_second()).raw_value(),
                         zx::sec(1).to_nsecs()));

    audio_clock_ =
        AudioClock::CreateAsDeviceStatic(clock::CloneOfMonotonic(), AudioClock::kMonotonicDomain);

    auto endpoints = BaseRingBuffer::AllocateSoftwareBuffer(
        format, std::move(timeline_function), reference_clock(), kRingBufferFrameCount, 0,
        [this]() { return safe_read_frame_; });
    ring_buffer_ = endpoints.reader;
    ASSERT_TRUE(ring_buffer());
  }

  ReadableRingBuffer* ring_buffer() const { return ring_buffer_.get(); }
  AudioClock& reference_clock() { return audio_clock_; }

  // Advance to the given time.
  void Advance(zx::time ref_time) {
    auto pts_to_frac_frame = ring_buffer_->ref_time_to_frac_presentation_frame().timeline_function;
    safe_read_frame_ = Fixed::FromRaw(pts_to_frac_frame.Apply(ref_time.get())).Floor();
  }

 private:
  std::shared_ptr<ReadableRingBuffer> ring_buffer_;

  AudioClock audio_clock_;
  int64_t safe_read_frame_ = -1;
};

class OutputRingBufferTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto& format = kDefaultFormat;
    auto timeline_function = fbl::MakeRefCounted<VersionedTimelineFunction>(
        TimelineFunction(0, zx::time(0).get(), Fixed(format.frames_per_second()).raw_value(),
                         zx::sec(1).to_nsecs()));

    audio_clock_ =
        AudioClock::CreateAsDeviceStatic(clock::CloneOfMonotonic(), AudioClock::kMonotonicDomain);

    auto endpoints = BaseRingBuffer::AllocateSoftwareBuffer(
        format, std::move(timeline_function), reference_clock(), kRingBufferFrameCount, 0,
        [this]() { return safe_write_frame_; });
    ring_buffer_ = endpoints.writer;
    ASSERT_TRUE(ring_buffer());
  }

  WritableRingBuffer* ring_buffer() const { return ring_buffer_.get(); }
  AudioClock& reference_clock() { return audio_clock_; }

  // Advance to the given time.
  void Advance(zx::time ref_time) {
    auto pts_to_frac_frame = ring_buffer_->ref_time_to_frac_presentation_frame().timeline_function;
    safe_write_frame_ = Fixed::FromRaw(pts_to_frac_frame.Apply(ref_time.get())).Floor();
  }

 private:
  std::shared_ptr<WritableRingBuffer> ring_buffer_;

  AudioClock audio_clock_;
  int64_t safe_write_frame_ = 0;
};

TEST_F(InputRingBufferTest, ReadEmptyRing) {
  Advance(zx::time(0));
  auto buffer = ring_buffer()->ReadLock(0, 1);
  ASSERT_FALSE(buffer);
}

TEST_F(InputRingBufferTest, ReadFullyExpiredBuffer) {
  // After 20ms, the ring will have been filled twice. If we request the first 480 frames then we
  // should get no buffer returned since all those frames are now unavailable.
  Advance(zx::time(0) + zx::msec(20));
  auto buffer = ring_buffer()->ReadLock(0, kRingBufferFrameCount);
  ASSERT_FALSE(buffer);
}

TEST_F(InputRingBufferTest, ReadNotYetAvailableBuffer) {
  // After 10ms, 480 frames will have been produced (0-479). The 480th frame is not yet available
  // so we should get no buffer.
  Advance(zx::time(0) + zx::msec(10));
  auto buffer = ring_buffer()->ReadLock(480, 1);
  ASSERT_FALSE(buffer);
}

TEST_F(InputRingBufferTest, ReadFullyAvailableRegion) {
  // After 1ms we expect 48 frames to be available to read at the start of the buffer.
  Advance(zx::time(0) + zx::msec(1));
  auto buffer = ring_buffer()->ReadLock(0, 48);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), 0u);
  EXPECT_EQ(buffer->length().Floor(), 48u);
}

TEST_F(InputRingBufferTest, ReadPartialRegion) {
  // After 1ms we expect 48 frames to be available to read at the start of the buffer. If we ask for
  // 96 we should get a buffer that contains only the 48 available frames.
  Advance(zx::time(0) + zx::msec(1));
  auto buffer = ring_buffer()->ReadLock(0, 96);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), 0u);
  EXPECT_EQ(buffer->length().Floor(), 48u);
}

TEST_F(InputRingBufferTest, SkipExpiredFrames) {
  // At 11ms we'll have written the entire ring + 48 more samples, so the first 48 frames are lost.
  // Test that the returned buffer correctly skips those 48 frames.
  Advance(zx::time(0) + zx::msec(11));
  auto buffer = ring_buffer()->ReadLock(0, 96);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), 48u);
  EXPECT_EQ(buffer->length().Floor(), 48u);
}

TEST_F(InputRingBufferTest, ReadAfterTruncateBufferAtEndOfTheRing) {
  // After 11ms 528 frames will have been put into the ring. We try to read the last 96 frames,
  // which spans the last 48 frames in the ring and then the first 48 frames at the start of the
  // ring again. Test our buffer is truncated for the first 48 frames requested at the end of the
  // ring.
  Advance(zx::time(0) + zx::msec(11));
  auto buffer = ring_buffer()->ReadLock(432, 96);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), 432u);
  EXPECT_EQ(buffer->length().Floor(), 48u);

  // Now read that last 48 frames at the start of the ring again.
  buffer = ring_buffer()->ReadLock(480, 48);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), 480u);
  EXPECT_EQ(buffer->length().Floor(), 48u);
}

TEST_F(InputRingBufferTest, ReadNegativeFrame) {
  Advance(zx::time(0));
  auto buffer = ring_buffer()->ReadLock(-10, 10);
  ASSERT_TRUE(buffer);

  auto rb_start_address = reinterpret_cast<uintptr_t>(ring_buffer()->virt());
  auto buffer_address = reinterpret_cast<uintptr_t>(buffer->payload());
  EXPECT_EQ(buffer_address,
            rb_start_address + ((ring_buffer()->frames() - 10) * kDefaultFormat.bytes_per_frame()));
  EXPECT_EQ(buffer->start().Floor(), -10);
  EXPECT_EQ(buffer->length().Floor(), 10u);
}

TEST_F(OutputRingBufferTest, WriteEmptyRing) {
  Advance(zx::time(0));
  auto buffer = ring_buffer()->WriteLock(0, 1);
  ASSERT_TRUE(buffer);
  ASSERT_EQ(0u, buffer->start().Floor());
  ASSERT_EQ(1u, buffer->length().Floor());
}

TEST_F(OutputRingBufferTest, WriteFullyExpiredBuffer) {
  // After 10ms the hardware will have already consumed the first kRingBufferFrameCount frames.
  // Attempting to get a buffer into that region should not be successful.
  Advance(zx::time(0) + zx::msec(10));
  auto buffer = ring_buffer()->WriteLock(0, kRingBufferFrameCount);
  ASSERT_FALSE(buffer);
}

TEST_F(OutputRingBufferTest, WriteNotYetAvailableBuffer) {
  // Trying to get a buffer more than |kRingBufferFrameCount| frames into the future will fail as it
  // would cause use to clobber frames not yet consumed by the hardware.
  Advance(zx::time(0));
  auto buffer = ring_buffer()->WriteLock(kRingBufferFrameCount, 1);
  ASSERT_FALSE(buffer);
}

TEST_F(OutputRingBufferTest, WriteFullyAvailableRegion) {
  // At time 0, we should be able to get a full buffer into the ring.
  Advance(zx::time(0));
  auto buffer = ring_buffer()->WriteLock(0, kRingBufferFrameCount);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), 0u);
  EXPECT_EQ(buffer->length().Floor(), kRingBufferFrameCount);
}

TEST_F(OutputRingBufferTest, WritePartialRegion) {
  // After 1ms we expect 48 frames to have been consumed, so we can only get a buffer into the final
  // 48 requested frames.
  Advance(zx::time(0) + zx::msec(1));
  auto buffer = ring_buffer()->WriteLock(0, 96);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), 48u);
  EXPECT_EQ(buffer->length().Floor(), 48u);
}

TEST_F(OutputRingBufferTest, WriteAfterTruncateBufferAtEndOfTheRing) {
  // After 9ms, the first 432 frames will have been consumed by hardware. Attempting to read the
  // next 96 frames will wrap around the ring, so we should only get the first 48 returned.
  Advance(zx::time(0) + zx::msec(9));
  auto buffer = ring_buffer()->WriteLock(432, 96);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), 432u);
  EXPECT_EQ(buffer->length().Floor(), 48u);

  // Now read that last 48 frames at the start of the ring again.
  buffer = ring_buffer()->WriteLock(kRingBufferFrameCount, 48);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), kRingBufferFrameCount);
  EXPECT_EQ(buffer->length().Floor(), 48u);
}

TEST(RingBufferTest, FrameOffset) {
  const auto& format = kDefaultFormat;
  const uint32_t frame_offset = 128;

  auto timeline_function = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
      0, zx::time(0).get(), Fixed(format.frames_per_second()).raw_value(), zx::sec(1).to_nsecs()));

  auto audio_clock = AudioClock::CreateAsCustom(clock::CloneOfMonotonic());

  auto endpoints =
      BaseRingBuffer::AllocateSoftwareBuffer(format, std::move(timeline_function), audio_clock,
                                             kRingBufferFrameCount, frame_offset, [] { return 0; });
  auto ring_buffer = std::move(endpoints.writer);
  ASSERT_TRUE(ring_buffer);

  // The first buffer section should be |frame_offset| into the physical ring buffer and can be at
  // most |kRingBufferFrameCount - frame_offset| frames long.
  auto buffer = ring_buffer->WriteLock(0, 2 * kRingBufferFrameCount);
  ASSERT_TRUE(buffer);
  ASSERT_EQ(0u, buffer->start().Floor());
  ASSERT_EQ(kRingBufferFrameCount - frame_offset, buffer->length().Floor());
  ASSERT_EQ(
      reinterpret_cast<uintptr_t>(buffer->payload()),
      reinterpret_cast<uintptr_t>(ring_buffer->virt()) + (frame_offset * format.bytes_per_frame()));

  // The second buffer portion back at the start of the physical ring.
  buffer = ring_buffer->WriteLock(kRingBufferFrameCount - frame_offset, 2 * kRingBufferFrameCount);
  ASSERT_TRUE(buffer);
  ASSERT_EQ(kRingBufferFrameCount - frame_offset, buffer->start().Floor());
  ASSERT_EQ(frame_offset, buffer->length().Floor());
  ASSERT_EQ(reinterpret_cast<uintptr_t>(buffer->payload()),
            reinterpret_cast<uintptr_t>(ring_buffer->virt()));
}

}  // namespace
}  // namespace media::audio
