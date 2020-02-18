// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/tap_stage.h"

#include <gmock/gmock.h>

#include "src/media/audio/audio_core/packet_queue.h"
#include "src/media/audio/audio_core/ring_buffer.h"
#include "src/media/audio/audio_core/testing/packet_factory.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"

using testing::Each;
using testing::FloatEq;

namespace media::audio {
namespace {

const Format kDefaultFormat =
    Format::Create(fuchsia::media::AudioStreamType{
                       .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                       .channels = 2,
                       .frames_per_second = 48000,
                   })
        .take_value();

constexpr uint32_t kRingBufferFrameCount = 1024;
constexpr uint32_t kDefaultPacketFrames = 480;

class TapStageTest : public testing::ThreadingModelFixture {
 protected:
  void SetUp() {
    testing::ThreadingModelFixture::SetUp();
    auto timeline_function = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
        TimelineRate(FractionalFrames<uint32_t>(kDefaultFormat.frames_per_second()).raw_value(),
                     zx::sec(1).to_nsecs())));
    packet_queue_ = std::make_shared<PacketQueue>(kDefaultFormat, timeline_function);
    ASSERT_TRUE(packet_queue_);

    auto endpoints = RingBuffer::AllocateSoftwareBuffer(kDefaultFormat, timeline_function,
                                                        kRingBufferFrameCount);
    ring_buffer_ = std::move(endpoints.reader);

    ASSERT_TRUE(ring_buffer_);

    tap_ = std::make_shared<TapStage>(packet_queue_, std::move(endpoints.writer));
    ClearRingBuffer();
  }

  template <typename T, size_t N>
  std::array<T, N>& as_array(void* ptr, size_t offset = 0) {
    return reinterpret_cast<std::array<T, N>&>(static_cast<T*>(ptr)[offset]);
  }

  void ClearRingBuffer(uint8_t value = 0) {
    memset(ring_buffer().virt(), value, ring_buffer().size());
  }

  TapStage& tap() { return *tap_; }
  PacketQueue& packet_queue() { return *packet_queue_; }
  testing::PacketFactory& packet_factory() { return packet_factory_; }
  RingBuffer& ring_buffer() { return *ring_buffer_; }

  // Assert that |stream| contains a buffer that is exactly |frame_count| frames starting at |frame|
  // with data that matches only |expected_sample| (that is all the samples in the buffer match
  // |expected_sample|).
  template <size_t frame_count>
  void CheckStream(Stream* stream, zx::duration epoch_delta, int64_t frame, float expected_sample,
                   bool release = true) {
    auto buffer = stream->LockBuffer(zx::time(0) + epoch_delta, frame, frame_count);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(buffer->start(), FractionalFrames<int64_t>(frame));
    EXPECT_EQ(buffer->length(), FractionalFrames<uint32_t>(frame_count));
    auto& arr = as_array<float, frame_count>(buffer->payload());
    EXPECT_THAT(arr, Each(FloatEq(expected_sample)));
    stream->UnlockBuffer(release);
  }

 private:
  testing::PacketFactory packet_factory_{dispatcher(), kDefaultFormat, 4 * PAGE_SIZE};
  std::shared_ptr<PacketQueue> packet_queue_;
  std::shared_ptr<RingBuffer> ring_buffer_;
  std::shared_ptr<TapStage> tap_;
};

TEST_F(TapStageTest, CopySinglePacket) {
  packet_queue().PushPacket(packet_factory().CreatePacket(1.0, zx::msec(10)));

  // We expect the tap and ring buffer to both be in sync for the first 480.
  constexpr size_t frame_count = kDefaultPacketFrames;
  CheckStream<frame_count>(&tap(), zx::msec(0), 0, 1.0, true);
  CheckStream<frame_count>(&ring_buffer(), zx::msec(10), 0, 1.0, true);
}

// Test that LockBuffer returns a buffer correctly sized for whatever buffer was returned by
// the source streams |LockBuffer|.
TEST_F(TapStageTest, TruncateToInputBuffer) {
  packet_queue().PushPacket(packet_factory().CreatePacket(1.0, zx::msec(10)));

  constexpr uint32_t frame_count = kDefaultPacketFrames;
  {  // Read from the tap, expect to get the same bytes from the packet.
    auto buffer = tap().LockBuffer(zx::time(0), 0, frame_count * 2);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(buffer->start(), FractionalFrames<int64_t>(0));
    EXPECT_EQ(buffer->length(), FractionalFrames<uint32_t>(frame_count));
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
  packet_queue().PushPacket(packet_factory().CreatePacket(1.0, zx::msec(10)));
  packet_queue().PushPacket(packet_factory().CreatePacket(2.0, zx::msec(10)));
  packet_queue().PushPacket(packet_factory().CreatePacket(3.0, zx::msec(10)));

  {  // With the first packet, we'll be fully in sync between the tap and the ring buffer.
    constexpr size_t frame_count = kDefaultPacketFrames;
    CheckStream<frame_count>(&tap(), zx::msec(0), 0, 1.0, true);
    CheckStream<frame_count>(&ring_buffer(), zx::msec(10), 0, 1.0, true);
  }

  {  // The second packet is still fully in sync between the tap and the ring buffer.
    constexpr size_t frame_count = kDefaultPacketFrames;
    CheckStream<frame_count>(&tap(), zx::msec(10), frame_count, 2.0, true);
    CheckStream<frame_count>(&ring_buffer(), zx::msec(20), frame_count, 2.0, true);
  }

  {  // For the final packet, we expect the Tap to return one buffer with the entire contents (this
    // is the packet buffer.
    constexpr size_t frame_count = kDefaultPacketFrames;
    CheckStream<frame_count>(&tap(), zx::msec(20), 2 * frame_count, 3.0, true);
  }

  // The ring buffer needs to be read in 2 portions, since this packet will wrap around the end of
  // the ring.
  //
  // The ring buffer should return the first 64 frames for the first LockBuffer (the only
  // remaining space before the buffer wraps around). A subsequent LockBuffer should return the
  // reminaing frames.
  constexpr uint32_t expected_frames_region_1 = kRingBufferFrameCount - (2 * kDefaultPacketFrames);
  constexpr uint32_t expected_frames_region_2 = kDefaultPacketFrames - expected_frames_region_1;
  {
    uint32_t requested_frames = kDefaultPacketFrames;
    constexpr uint32_t expected_frames = expected_frames_region_1;
    int64_t frame = 2 * kDefaultPacketFrames;
    auto buffer = ring_buffer().LockBuffer(zx::time(0) + zx::msec(30), frame, requested_frames);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(buffer->start(), FractionalFrames<int64_t>(frame));
    EXPECT_EQ(buffer->length(), FractionalFrames<uint32_t>(expected_frames));
    auto& arr = as_array<float, expected_frames>(buffer->payload());
    EXPECT_THAT(arr, Each(FloatEq(3.0f)));
  }

  {
    constexpr uint32_t requested_frames = kDefaultPacketFrames;
    constexpr uint32_t expected_frames = expected_frames_region_2;
    int64_t frame = kRingBufferFrameCount;
    auto buffer = ring_buffer().LockBuffer(zx::time(0) + zx::msec(30), frame, requested_frames);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(buffer->start(), FractionalFrames<int64_t>(frame));
    EXPECT_EQ(buffer->length(), FractionalFrames<uint32_t>(expected_frames));
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

}  // namespace
}  // namespace media::audio
