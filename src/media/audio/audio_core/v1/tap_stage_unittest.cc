// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/tap_stage.h"

#include <gmock/gmock.h>

#include "src/media/audio/audio_core/v1/packet_queue.h"
#include "src/media/audio/audio_core/v1/ring_buffer.h"
#include "src/media/audio/audio_core/v1/testing/packet_factory.h"
#include "src/media/audio/audio_core/v1/testing/threading_model_fixture.h"
#include "src/media/audio/lib/clock/clone_mono.h"

using testing::Each;
using testing::FloatEq;

namespace media::audio {
namespace {

// Used when the ReadLockContext is unused by the test.
static media::audio::ReadableStream::ReadLockContext rlctx;

constexpr uint32_t kChannels = 2;
const Format kDefaultFormat =
    Format::Create(fuchsia::media::AudioStreamType{
                       .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                       .channels = kChannels,
                       .frames_per_second = 48000,
                   })
        .take_value();

constexpr uint32_t kRingBufferFrameCount = 1024;
constexpr uint32_t kPacketFrames = 480;
constexpr zx::duration kPacketDuration = zx::msec(10);

class TapStageTest : public testing::ThreadingModelFixture {
 protected:
  TapStageTest() : TapStageTest(0) {}

  // tap_frame = source_frame + tap_frame_offset.
  //
  // This is used to test that TapStage can correctly convert between arbitrary timelines.
  TapStageTest(uint32_t tap_frame_offset) : tap_frame_offset_(tap_frame_offset) {}

  void SetUp() {
    testing::ThreadingModelFixture::SetUp();
    TimelineRate rate(Fixed(kDefaultFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs());
    auto source_timeline_function =
        fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(rate));

    packet_queue_ = std::make_shared<PacketQueue>(
        kDefaultFormat, source_timeline_function,
        context().clock_factory()->CreateClientFixed(clock::AdjustableCloneOfMonotonic()));
    ASSERT_TRUE(packet_queue_);

    auto tap_timeline_function =
        fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(0, 0, rate));

    auto endpoints = BaseRingBuffer::AllocateSoftwareBuffer(
        kDefaultFormat, tap_timeline_function, packet_queue_->reference_clock(),
        kRingBufferFrameCount, [this] { return safe_write_frame_; });
    ring_buffer_ = std::move(endpoints.reader);

    ASSERT_TRUE(ring_buffer_);

    tap_ = std::make_shared<TapStage>(packet_queue_, std::move(endpoints.writer));
    ClearRingBuffer();
  }

  template <typename T, size_t N>
  std::array<T, N>& as_array(void* ptr, size_t offset = 0) {
    return reinterpret_cast<std::array<T, N>&>(static_cast<T*>(ptr)[offset]);
  }

  void ClearRingBuffer(float value = 0.0) {
    for (size_t i = 0; i < ring_buffer().size() / sizeof(float); ++i) {
      reinterpret_cast<float*>(ring_buffer().virt())[i] = value;
    }
  }

  void AdvanceTo(zx::duration d) {
    auto ref_time = zx::time(0) + d;
    auto pts_to_frac_frame = ring_buffer_->ref_time_to_frac_presentation_frame().timeline_function;
    safe_write_frame_ = Fixed::FromRaw(pts_to_frac_frame.Apply(ref_time.get())).Floor();
  }

  TapStage& tap() { return *tap_; }
  PacketQueue& packet_queue() { return *packet_queue_; }
  testing::PacketFactory& packet_factory() { return packet_factory_; }
  ReadableRingBuffer& ring_buffer() { return *ring_buffer_; }

  template <int64_t frame_count>
  void CheckBuffer(const ReadableStream::Buffer& buffer, int64_t frame, float expected_sample) {
    EXPECT_EQ(buffer.start(), Fixed(frame));
    EXPECT_EQ(buffer.length(), Fixed(frame_count));
    auto& arr = as_array<float, frame_count * kChannels>(buffer.payload());
    EXPECT_THAT(arr, Each(FloatEq(expected_sample)));
  }

  // Assert that |stream| contains a buffer that is exactly |frame_count| frames starting at |frame|
  // with data that matches only |expected_sample| (that is all the samples in the buffer match
  // |expected_sample|).
  template <int64_t frame_count>
  void CheckStream(ReadableStream* stream, int64_t frame, float expected_sample,
                   bool release = true) {
    auto buffer = stream->ReadLock(rlctx, Fixed(frame), frame_count);
    ASSERT_TRUE(buffer);
    CheckBuffer<frame_count>(*buffer, frame, expected_sample);
    if (!release) {
      buffer->set_frames_consumed(0);
    }
  }

  uint32_t tap_frame_offset_;
  testing::PacketFactory packet_factory_{dispatcher(), kDefaultFormat,
                                         4 * zx_system_get_page_size()};
  std::shared_ptr<PacketQueue> packet_queue_;
  std::shared_ptr<ReadableRingBuffer> ring_buffer_;
  std::shared_ptr<TapStage> tap_;
  int64_t safe_write_frame_ = 0;
};

TEST_F(TapStageTest, CopySinglePacket) {
  packet_queue().PushPacket(packet_factory().CreatePacket(1.0, kPacketDuration));

  // We expect the tap and ring buffer to both be in sync for the first 480.
  constexpr int64_t frame_count = kPacketFrames;
  CheckStream<frame_count>(&tap(), 0, 1.0, true);
  AdvanceTo(kPacketDuration);
  CheckStream<frame_count>(&ring_buffer(), 0, 1.0, true);
}

TEST_F(TapStageTest, CopySinglePacketWithFractionalDestPosition) {
  packet_queue().PushPacket(packet_factory().CreatePacket(1.0, kPacketDuration));

  // Like CopySinglePacket, but use dest_frame = 0.5.
  constexpr int64_t frame_count = kPacketFrames;
  {
    auto buffer = tap().ReadLock(rlctx, Fixed(0) + ffl::FromRatio(1, 2), frame_count);
    ASSERT_TRUE(buffer);
    CheckBuffer<frame_count>(*buffer, 0, 1.0);
  }
  AdvanceTo(kPacketDuration);
  {
    auto buffer = ring_buffer().ReadLock(rlctx, Fixed(0) + ffl::FromRatio(1, 2), frame_count);
    ASSERT_TRUE(buffer);
    CheckBuffer<frame_count>(*buffer, 0, 1.0);
  }
}

TEST_F(TapStageTest, CopySinglePacketWithFractionalSourcePosition) {
  // Seek to one packet - 0.5 frames.
  packet_factory().SeekToFrame(Fixed(kPacketFrames) - ffl::FromRatio(1, 2));
  packet_queue().PushPacket(packet_factory().CreatePacket(1.0, kPacketDuration));

  // The tap and ring should have one packet of silence followed by one packet of data.
  {
    auto buffer = tap().ReadLock(rlctx, Fixed(0), kPacketFrames);
    ASSERT_FALSE(buffer);
  }
  AdvanceTo(kPacketDuration);
  {
    auto buffer = tap().ReadLock(rlctx, Fixed(kPacketFrames), kPacketFrames);
    ASSERT_TRUE(buffer);
    CheckBuffer<kPacketFrames>(*buffer, kPacketFrames, 1.0);
  }
  AdvanceTo(kPacketDuration * 2);
  CheckStream<kPacketFrames>(&ring_buffer(), 0, 0.0, true);
  CheckStream<kPacketFrames>(&ring_buffer(), kPacketFrames, 1.0, true);
}

// Test that ReadLock returns a buffer correctly sized for whatever buffer was returned by
// the source streams |ReadLock|.
TEST_F(TapStageTest, TruncateToInputBuffer) {
  packet_queue().PushPacket(packet_factory().CreatePacket(1.0, kPacketDuration));

  constexpr int64_t frame_count = kPacketFrames;
  {  // Read from the tap, expect to get the same bytes from the packet.
    auto buffer = tap().ReadLock(rlctx, Fixed(0), frame_count * 2);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(buffer->start(), Fixed(0));
    EXPECT_EQ(buffer->length(), Fixed(frame_count));
    auto& arr = as_array<float, frame_count>(buffer->payload());
    EXPECT_THAT(arr, Each(FloatEq(1.0f)));
  }
}

// Test the case where a single input buffer will require 2 writes to the ring buffer as the buffer
// will cross the end of the ring.
TEST_F(TapStageTest, WrapAroundRingBuffer) {
  // The ring is 1024 frames. So we write:
  //   0 -  479 = 1.0 samples
  // 480 -  959 = 2.0 samples
  // 960 - 1023 = 3.0 samples
  //   0 -  415 = 3.0 samples (3rd packet wrapped around.
  packet_queue().PushPacket(packet_factory().CreatePacket(1.0, kPacketDuration));
  packet_queue().PushPacket(packet_factory().CreatePacket(2.0, kPacketDuration));
  packet_queue().PushPacket(packet_factory().CreatePacket(3.0, kPacketDuration));

  {  // With the first packet, we'll be fully in sync between the tap and the ring buffer.
    constexpr int64_t frame_count = kPacketFrames;
    SCOPED_TRACE("first packet in tap");
    CheckStream<frame_count>(&tap(), 0, 1.0, true);
    SCOPED_TRACE("first packet in ring buffer");
    AdvanceTo(kPacketDuration);
    CheckStream<frame_count>(&ring_buffer(), 0, 1.0, true);
  }

  {  // The second packet is still fully in sync between the tap and the ring buffer.
    constexpr int64_t frame_count = kPacketFrames;
    SCOPED_TRACE("second packet in tap");
    CheckStream<frame_count>(&tap(), frame_count, 2.0, true);
    SCOPED_TRACE("second packet in ring buffer");
    AdvanceTo(zx::msec(20));
    CheckStream<frame_count>(&ring_buffer(), frame_count, 2.0, true);
  }

  {  // For the final packet, we expect the Tap to return one buffer with the entire contents (this
    // is the packet buffer.
    constexpr int64_t frame_count = kPacketFrames;
    SCOPED_TRACE("final packet in tap");
    CheckStream<frame_count>(&tap(), 2 * frame_count, 3.0, true);
  }

  AdvanceTo(zx::msec(30));

  // The ring buffer needs to be read in 2 portions, since this packet will wrap around the end of
  // the ring.
  //
  // The ring buffer should return the first 64 frames for the first ReadLock (the only
  // remaining space before the buffer wraps around). A subsequent ReadLock should return the
  // remaining frames.
  constexpr uint32_t expected_frames_region_1 = kRingBufferFrameCount - (2 * kPacketFrames);
  constexpr uint32_t expected_frames_region_2 = kPacketFrames - expected_frames_region_1;
  {
    uint32_t requested_frames = kPacketFrames;
    constexpr uint32_t expected_frames = expected_frames_region_1;
    int64_t frame = 2 * kPacketFrames;
    auto buffer = ring_buffer().ReadLock(rlctx, Fixed(frame), requested_frames);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(buffer->start(), Fixed(frame));
    EXPECT_EQ(buffer->length(), Fixed(expected_frames));
    auto& arr = as_array<float, expected_frames>(buffer->payload());
    EXPECT_THAT(arr, Each(FloatEq(3.0f)));
  }

  {
    constexpr uint32_t requested_frames = kPacketFrames;
    constexpr uint32_t expected_frames = expected_frames_region_2;
    int64_t frame = kRingBufferFrameCount;
    auto buffer = ring_buffer().ReadLock(rlctx, Fixed(frame), requested_frames);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(buffer->start(), Fixed(frame));
    EXPECT_EQ(buffer->length(), Fixed(expected_frames));
    {
      auto& arr = as_array<float, expected_frames>(buffer->payload());
      EXPECT_THAT(arr, Each(FloatEq(3.0f)));
    }
    {
      constexpr uint32_t len = requested_frames - expected_frames;
      auto& arr = as_array<float, len>(buffer->payload(),
                                       expected_frames * ring_buffer().format().channels());
      EXPECT_THAT(arr, Each(FloatEq(1.0f)));
    }
  }
}

TEST_F(TapStageTest, PartialTapBuffer) {
  constexpr float kInitialRingBufferColor = 5.0;
  ClearRingBuffer(kInitialRingBufferColor);

  // Test the case where part of a tap buffer is unavailable because of the safe write pointer has
  // moved beyond that frame.
  //
  // Seek the tap buffer so that the first half packet is not available in the ring buffer. We
  // expect these frames to be untouched in the underlying buffer.
  AdvanceTo(kPacketDuration / 2);

  // Pull a full packet through the the tap stage.
  packet_queue().PushPacket(packet_factory().CreatePacket(1.0, kPacketDuration));

  // Expect the full packet to be read out of the tap stage.
  SCOPED_TRACE("first packet in tap");
  CheckStream<kPacketFrames>(&tap(), 0, 1.0, true);

  // Expect the first half of the packet to be untouched in the ring buffer.
  SCOPED_TRACE("unmodified ring buffer region");
  CheckStream<kPacketFrames / 2>(&ring_buffer(), 0, kInitialRingBufferColor, true);

  // But the second half should match the input.
  SCOPED_TRACE("modified ring buffer region");
  AdvanceTo(kPacketDuration);
  CheckStream<kPacketFrames / 2>(&ring_buffer(), kPacketFrames / 2, 1.0, true);

  // And anything after that should also be unmodified.
  SCOPED_TRACE("silence after tap");
  AdvanceTo(kPacketDuration * 2);
  CheckStream<kPacketFrames>(&ring_buffer(), kPacketFrames, kInitialRingBufferColor, true);
}

TEST_F(TapStageTest, ShortSourceBuffer) {
  constexpr float kSourceBufferColor = 3.0;
  constexpr float kInitialRingBufferColor = 5.0;

  // Clear the buffer with a defined bit-pattern. This is so that we may detect if any frames have
  // or have not been modified by the TapStage.
  ClearRingBuffer(kInitialRingBufferColor);

  // Enqueue a single buffer that is half as long as the packet we will read out of the TapStage,
  // and also offset by a quarter packet of implicit silence:
  //
  //   ---------------------------
  //  |              1            |
  //  |---------------------------|
  //  |  2   |~~~~~~~~~~~~~~~~~~~~|
  //  |---------------------------|
  //  |~~~~~~|      3      |~~~~~~|
  //  |---------------------------|
  //  |~~~~~~~~~~~~~~~~~~~~|  4   |
  //   ---------------------------
  //  ^                           ^
  //  0                      kPacketFrames
  //
  // Where
  //   Region 1 -- The requested frames from the TapStage, which spans frames 0 to kPacketFrames.
  //   Region 2 -- The region before the first frame in source. This should be replicated into the
  //               tap stage as silence.
  //   Region 3 -- Frames available in source that need to be copied into the tap stream.
  //   Region 4 -- Frames that are beyond the end of the source stream packet. We expect the buffer
  //               returned from TapStage to end before this region, and we expect the corresponding
  //               frames in the ring buffer to be unmodified.

  // Create region '2' in the source stream by seeking past these frames. The next 'CreatePacket'
  // will occur after this.
  packet_factory().SeekToFrame(Fixed(kPacketFrames / 4));
  // Create region '3' in the source stream.
  packet_queue().PushPacket(packet_factory().CreatePacket(kSourceBufferColor, kPacketDuration / 2));

  // Request 'region 1'
  auto buffer = tap().ReadLock(rlctx, Fixed(0), kPacketFrames);
  // But expect 'region 3', which corresponds to what is available in source.
  ASSERT_TRUE(buffer);
  CheckBuffer<kPacketFrames / 2>(*buffer, kPacketFrames / 4, 3.0);

  AdvanceTo(kPacketDuration);

  // Verify the silence from 'region 2' is available in the tap stream.
  SCOPED_TRACE("silence in ring buffer");
  CheckStream<kPacketFrames / 4>(&ring_buffer(), 0, 0.0, true);

  // Followed by 'region 3'
  SCOPED_TRACE("frames from source in ring buffer");
  CheckStream<kPacketFrames / 2>(&ring_buffer(), kPacketFrames / 4, kSourceBufferColor, true);

  // Ensure that frames in 'region 4' were not modified in the ring buffer.
  SCOPED_TRACE("unmodified frames");
  CheckStream<kPacketFrames / 4>(&ring_buffer(), 3 * kPacketFrames / 4, kInitialRingBufferColor,
                                 true);
}

TEST_F(TapStageTest, SilentSourceStream) {
  // Initialize the buffer to something that is not silence.
  ClearRingBuffer(5.0);

  // Read from the ring buffer.
  auto buffer = tap().ReadLock(rlctx, Fixed(0), kPacketFrames);

  // Our packet queue source is empty so we expect no data here.
  EXPECT_FALSE(buffer);

  // But we do expect the frames in our tap to have been written silence.
  AdvanceTo(kPacketDuration);
  CheckStream<kPacketFrames>(&ring_buffer(), 0, 0.0, true);
}

TEST_F(TapStageTest, ShortTapBuffer) {
  constexpr float kSourceBufferColor = 3.0;
  constexpr float kInitialRingBufferColor = 5.0;

  // Clear the buffer with a defined bit-pattern. This is so that we may detect if any frames have
  // or have not been modified by the TapStage.
  ClearRingBuffer(kInitialRingBufferColor);

  // Create region '2' in the source stream by seeking past these frames. The next 'CreatePacket'
  // will occur after this.
  packet_factory().SeekToFrame(Fixed(kPacketFrames / 4));
  // Create region '3' in the source stream.
  packet_queue().PushPacket(packet_factory().CreatePacket(kSourceBufferColor, kPacketDuration / 2));

  // Request 'region 1'
  auto buffer = tap().ReadLock(rlctx, Fixed(0), kPacketFrames);
  // But expect 'region 3', which corresponds to what is available in source.
  ASSERT_TRUE(buffer);
  CheckBuffer<kPacketFrames / 2>(*buffer, kPacketFrames / 4, 3.0);

  AdvanceTo(kPacketDuration);

  // Verify the silence from 'region 2' is available in the tap stream.
  SCOPED_TRACE("silence in ring buffer");
  CheckStream<kPacketFrames / 4>(&ring_buffer(), 0, 0.0, true);

  // Followed by 'region 3'
  SCOPED_TRACE("frames from source in ring buffer");
  CheckStream<kPacketFrames / 2>(&ring_buffer(), kPacketFrames / 4, kSourceBufferColor, true);

  // Ensure that frames in 'region 4' were not modified in the ring buffer.
  SCOPED_TRACE("unmodified frames");
  CheckStream<kPacketFrames / 4>(&ring_buffer(), 3 * kPacketFrames / 4, kInitialRingBufferColor,
                                 true);
}

class TapStageFrameConversionTest : public TapStageTest {
 protected:
  TapStageFrameConversionTest() : TapStageTest(12345) {}
};

// Test that we can properly copy a packet when the source and tap streams are using different
// TimelineFunctions.
TEST_F(TapStageFrameConversionTest, CopySinglePacket) {
  packet_queue().PushPacket(packet_factory().CreatePacket(1.0, kPacketDuration));

  // We expect the tap and ring buffer to both be in sync for the first 480.
  constexpr int64_t frame_count = kPacketFrames;
  CheckStream<frame_count>(&tap(), 0, 1.0, true);
  AdvanceTo(kPacketDuration);
  CheckStream<frame_count>(&ring_buffer(), 0, 1.0, true);
}

}  // namespace
}  // namespace media::audio
