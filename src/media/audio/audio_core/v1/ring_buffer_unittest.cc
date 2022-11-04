// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/ring_buffer.h"

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/v1/testing/fake_audio_core_clock_factory.h"

namespace media::audio {
namespace {

// Used when the ReadLockContext is unused by the test.
static media::audio::ReadableStream::ReadLockContext rlctx;

const Format kDefaultFormat =
    Format::Create(fuchsia::media::AudioStreamType{
                       .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                       .channels = 2,
                       .frames_per_second = 48000,
                   })
        .take_value();

// 10ms @ 48khz
const int64_t kRingBufferFrameCount = 480;
const auto kRingBufferFrameDuration = zx::msec(10);

class InputRingBufferTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto& format = kDefaultFormat;
    auto timeline_function = fbl::MakeRefCounted<VersionedTimelineFunction>(
        TimelineFunction(0, zx::time(0).get(), Fixed(format.frames_per_second()).raw_value(),
                         zx::sec(1).to_nsecs()));

    auto endpoints = BaseRingBuffer::AllocateSoftwareBuffer(
        format, std::move(timeline_function), reference_clock(), kRingBufferFrameCount,
        [this]() { return safe_write_frame_; });
    ring_buffer_ = endpoints.reader;
    ASSERT_TRUE(ring_buffer());
  }

  ReadableRingBuffer* ring_buffer() const { return ring_buffer_.get(); }
  std::shared_ptr<Clock> reference_clock() { return audio_clock_; }

  // Advance to the given time.
  void Advance(zx::time ref_time) {
    auto pts_to_frac_frame = ring_buffer_->ref_time_to_frac_presentation_frame().timeline_function;
    safe_write_frame_ = Fixed::FromRaw(pts_to_frac_frame.Apply(ref_time.get())).Floor();
  }

 private:
  std::shared_ptr<ReadableRingBuffer> ring_buffer_;

  std::shared_ptr<Clock> audio_clock_ =
      ::media::audio::testing::FakeAudioCoreClockFactory::DefaultClock();

  int64_t safe_write_frame_ = std::numeric_limits<int64_t>::min();
};

class OutputRingBufferTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto& format = kDefaultFormat;
    auto timeline_function = fbl::MakeRefCounted<VersionedTimelineFunction>(
        TimelineFunction(0, zx::time(0).get(), Fixed(format.frames_per_second()).raw_value(),
                         zx::sec(1).to_nsecs()));

    auto endpoints = BaseRingBuffer::AllocateSoftwareBuffer(
        format, std::move(timeline_function), reference_clock(), kRingBufferFrameCount,
        [this]() { return safe_write_frame_; });
    ring_buffer_ = endpoints.writer;
    ASSERT_TRUE(ring_buffer());
  }

  WritableRingBuffer* ring_buffer() const { return ring_buffer_.get(); }
  std::shared_ptr<Clock> reference_clock() { return audio_clock_; }

  // Advance to the given time.
  void Advance(zx::time ref_time) {
    auto pts_to_frac_frame = ring_buffer_->ref_time_to_frac_presentation_frame().timeline_function;
    safe_write_frame_ = Fixed::FromRaw(pts_to_frac_frame.Apply(ref_time.get())).Floor();
  }

 private:
  std::shared_ptr<WritableRingBuffer> ring_buffer_;

  std::shared_ptr<Clock> audio_clock_ =
      ::media::audio::testing::FakeAudioCoreClockFactory::DefaultClock();
  int64_t safe_write_frame_ = 0;
};

}  // namespace

TEST_F(InputRingBufferTest, ReadEmptyRing) {
  Advance(zx::time(0));
  auto buffer = ring_buffer()->ReadLock(rlctx, Fixed(0), 1);
  ASSERT_FALSE(buffer);
}

TEST_F(InputRingBufferTest, ReadFullyExpiredBuffer) {
  // After 20ms, the ring will have been filled twice. If we request the first 480 frames then we
  // should get no buffer returned since all those frames are now unavailable.
  Advance(zx::time(0) + zx::msec(20));
  auto buffer = ring_buffer()->ReadLock(rlctx, Fixed(0), kRingBufferFrameCount);
  ASSERT_FALSE(buffer);
}

TEST_F(InputRingBufferTest, ReadNotYetAvailableBuffer) {
  // After 10ms, 480 frames will have been produced (0-479). The 480th frame is not yet available
  // so we should get no buffer.
  Advance(zx::time(0) + zx::msec(10));
  auto buffer = ring_buffer()->ReadLock(rlctx, Fixed(480), 1);
  ASSERT_FALSE(buffer);
}

TEST_F(InputRingBufferTest, ReadFullyAvailableRegion) {
  // After 1ms we expect 48 frames to be available to read at the start of the buffer.
  Advance(zx::time(0) + zx::msec(1));
  auto buffer = ring_buffer()->ReadLock(rlctx, Fixed(0), 48);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), 0);
  EXPECT_EQ(buffer->length(), 48);
}

TEST_F(InputRingBufferTest, ReadPartialRegion) {
  // After 1ms we expect 48 frames to be available to read at the start of the buffer. If we ask for
  // 96 we should get a buffer that contains only the 48 available frames.
  Advance(zx::time(0) + zx::msec(1));
  auto buffer = ring_buffer()->ReadLock(rlctx, Fixed(0), 96);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), 0);
  EXPECT_EQ(buffer->length(), 48);
}

TEST_F(InputRingBufferTest, SkipExpiredFrames) {
  // At 11ms we'll have written the entire ring + 48 more samples, so the first 48 frames are lost.
  // Test that the returned buffer correctly skips those 48 frames.
  Advance(zx::time(0) + zx::msec(11));
  auto buffer = ring_buffer()->ReadLock(rlctx, Fixed(0), 96);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start().Floor(), 48);
  EXPECT_EQ(buffer->length(), 48);
}

TEST_F(InputRingBufferTest, ReadAfterTruncateBufferAtEndOfTheRing) {
  // After 11ms 528 frames will have been put into the ring. We try to read the last 96 frames,
  // which spans the last 48 frames in the ring and then the first 48 frames at the start of the
  // ring again. Test our buffer is truncated for the first 48 frames requested at the end of the
  // ring.
  Advance(zx::time(0) + zx::msec(11));
  {
    auto buffer = ring_buffer()->ReadLock(rlctx, Fixed(432), 96);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(buffer->start().Floor(), 432);
    EXPECT_EQ(buffer->length(), 48);
  }

  // Now read that last 48 frames at the start of the ring again.
  {
    auto buffer = ring_buffer()->ReadLock(rlctx, Fixed(480), 48);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(buffer->start().Floor(), 480);
    EXPECT_EQ(buffer->length(), 48);
  }
}

TEST_F(InputRingBufferTest, ReadNegativeFrames) {
  // Allow reading [-2*RingBufFrames, -RingBufFrames).
  Advance(zx::time(0) - kRingBufferFrameDuration);

  // Request [-20, -10) but shifted down by kRingBufferFrameCount to test modulo.
  const int64_t kRingBufferFrameCount = ring_buffer()->frames();
  auto buffer = ring_buffer()->ReadLock(rlctx, Fixed(-kRingBufferFrameCount - 20), 10);
  ASSERT_TRUE(buffer);

  auto rb_start_address = reinterpret_cast<uintptr_t>(ring_buffer()->virt());
  auto buffer_address = reinterpret_cast<uintptr_t>(buffer->payload());
  EXPECT_EQ(buffer_address,
            rb_start_address + (kRingBufferFrameCount - 20) * kDefaultFormat.bytes_per_frame());
  EXPECT_EQ(buffer->start().Floor(), -kRingBufferFrameCount - 20);
  EXPECT_EQ(buffer->length(), 10);
}

TEST_F(InputRingBufferTest, ReadNegativeThroughPositiveFrame) {
  // Allow reading [-RingBufFrames, 0).
  Advance(zx::time(0));

  // Request [-5, 5).
  const int64_t kRingBufferFrameCount = ring_buffer()->frames();
  auto buffer = ring_buffer()->ReadLock(rlctx, Fixed(-5), 10);
  ASSERT_TRUE(buffer);

  // Should return [-5, 0) since the ring buffer wraps at 0.
  auto rb_start_address = reinterpret_cast<uintptr_t>(ring_buffer()->virt());
  auto buffer_address = reinterpret_cast<uintptr_t>(buffer->payload());
  EXPECT_EQ(buffer_address,
            rb_start_address + (kRingBufferFrameCount - 5) * kDefaultFormat.bytes_per_frame());
  EXPECT_EQ(buffer->start().Floor(), -5);
  EXPECT_EQ(buffer->length(), 5);
}

TEST_F(InputRingBufferTest, ReadFromDup) {
  // After 1ms we expect 48 frames to be available to read at the start of the buffer.
  Advance(zx::time(0) + zx::msec(1));
  {
    auto buffer = ring_buffer()->ReadLock(rlctx, Fixed(0), 48);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(buffer->start().Floor(), 0);
    EXPECT_EQ(buffer->length(), 48);
  }

  // After trimming, we can no longer read those frames from the stream.
  ring_buffer()->Trim(Fixed(48));

  // However, we should be able to read those frames from a dup.
  auto dup = ring_buffer()->Dup();
  {
    auto buffer = dup->ReadLock(rlctx, Fixed(0), 48);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(buffer->start().Floor(), 0);
    EXPECT_EQ(buffer->length(), 48);
  }
}

TEST_F(OutputRingBufferTest, WriteEmptyRing) {
  Advance(zx::time(0));
  auto buffer = ring_buffer()->WriteLock(0, 1);
  ASSERT_TRUE(buffer);
  ASSERT_EQ(buffer->start(), 0);
  ASSERT_EQ(buffer->length(), 1);
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
  EXPECT_EQ(buffer->start(), 0);
  EXPECT_EQ(buffer->length(), kRingBufferFrameCount);
}

TEST_F(OutputRingBufferTest, WritePartialRegion) {
  // After 1ms we expect 48 frames to have been consumed, so we can only get a buffer into the final
  // 48 requested frames.
  Advance(zx::time(0) + zx::msec(1));
  auto buffer = ring_buffer()->WriteLock(0, 96);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start(), 48);
  EXPECT_EQ(buffer->length(), 48);
}

TEST_F(OutputRingBufferTest, WriteAfterTruncateBufferAtEndOfTheRing) {
  // After 9ms, the first 432 frames will have been consumed by hardware. Attempting to read the
  // next 96 frames will wrap around the ring, so we should only get the first 48 returned.
  Advance(zx::time(0) + zx::msec(9));
  auto buffer = ring_buffer()->WriteLock(432, 96);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start(), 432);
  EXPECT_EQ(buffer->length(), 48);

  // Now read that last 48 frames at the start of the ring again.
  buffer = ring_buffer()->WriteLock(kRingBufferFrameCount, 48);
  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->start(), kRingBufferFrameCount);
  EXPECT_EQ(buffer->length(), 48);
}

}  // namespace media::audio
