// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/ring_buffer.h"

#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/time.h>

#include <memory>
#include <optional>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/mix/packet_view.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"

namespace media_audio {
namespace {

using ::fuchsia_mediastreams::wire::AudioSampleFormat;

const Format kFormat = Format::CreateOrDie({AudioSampleFormat::kFloat, 2, 48000});
constexpr int64_t kRingBufferFrames = 100;

// Since Read and PrepareToWrite have the same implementations (ignoring cache invalidation and
// flushing, which are hard to test), it's sufficient to test Read only.
class RingBufferTest : public ::testing::Test {
 public:
  MemoryMappedBuffer& buffer() { return *buffer_; }
  RingBuffer& ring_buffer() { return *ring_buffer_; }

 private:
  std::shared_ptr<MemoryMappedBuffer> buffer_ =
      MemoryMappedBuffer::CreateOrDie(kRingBufferFrames * kFormat.bytes_per_frame(), true);
  std::shared_ptr<RingBuffer> ring_buffer_ = RingBuffer::Create({
      .format = kFormat,
      .reference_clock = DefaultClock(),
      .buffer = buffer_,
      .producer_frames = kRingBufferFrames / 2,
      .consumer_frames = kRingBufferFrames / 2,
  });
};

TEST_F(RingBufferTest, ReadUnwrappedFromStart) {
  const auto packet = ring_buffer().Read(0, 50);
  EXPECT_EQ(packet.start(), Fixed(0));
  EXPECT_EQ(packet.length(), 50);
  EXPECT_EQ(packet.payload(), buffer().offset(0));
}

TEST_F(RingBufferTest, ReadUnwrappedFromMiddle) {
  const auto packet = ring_buffer().Read(50, 10);
  EXPECT_EQ(packet.start(), Fixed(50));
  EXPECT_EQ(packet.length(), 10);
  EXPECT_EQ(packet.payload(), buffer().offset(50 * kFormat.bytes_per_frame()));
}

TEST_F(RingBufferTest, ReadUnwrappedFromEnd) {
  const auto packet = ring_buffer().Read(90, 10);
  EXPECT_EQ(packet.start(), Fixed(90));
  EXPECT_EQ(packet.length(), 10);
  EXPECT_EQ(packet.payload(), buffer().offset(90 * kFormat.bytes_per_frame()));
}

TEST_F(RingBufferTest, ReadUnwrappedOverlapsEnd) {
  const auto packet = ring_buffer().Read(95, 10);
  EXPECT_EQ(packet.start(), Fixed(95));
  EXPECT_EQ(packet.length(), 5);
  EXPECT_EQ(packet.payload(), buffer().offset(95 * kFormat.bytes_per_frame()));
}

TEST_F(RingBufferTest, ReadWrappedFromStart) {
  const auto packet = ring_buffer().Read(100, 10);
  EXPECT_EQ(packet.start(), Fixed(100));
  EXPECT_EQ(packet.length(), 10);
  EXPECT_EQ(packet.payload(), buffer().offset(0));
}

TEST_F(RingBufferTest, ReadWrappedFromMiddle) {
  const auto packet = ring_buffer().Read(150, 10);
  EXPECT_EQ(packet.start(), Fixed(150));
  EXPECT_EQ(packet.length(), 10);
  EXPECT_EQ(packet.payload(), buffer().offset(50 * kFormat.bytes_per_frame()));
}

TEST_F(RingBufferTest, ReadWrappedFromEnd) {
  const auto packet = ring_buffer().Read(190, 10);
  EXPECT_EQ(packet.start(), Fixed(190));
  EXPECT_EQ(packet.length(), 10);
  EXPECT_EQ(packet.payload(), buffer().offset(90 * kFormat.bytes_per_frame()));
}

TEST_F(RingBufferTest, ReadWrappedOverlapsEnd) {
  const auto packet = ring_buffer().Read(195, 10);
  EXPECT_EQ(packet.start(), Fixed(195));
  EXPECT_EQ(packet.length(), 5);
  EXPECT_EQ(packet.payload(), buffer().offset(95 * kFormat.bytes_per_frame()));
}

TEST_F(RingBufferTest, ReadNegativeFrames) {
  const auto packet = ring_buffer().Read(-10, 10);
  EXPECT_EQ(packet.start(), Fixed(-10));
  EXPECT_EQ(packet.length(), 10);
  EXPECT_EQ(packet.payload(), buffer().offset(90 * kFormat.bytes_per_frame()));
}

TEST_F(RingBufferTest, ReadVeryNegativeFrames) {
  const auto packet = ring_buffer().Read(-110, 10);
  EXPECT_EQ(packet.start(), Fixed(-110));
  EXPECT_EQ(packet.length(), 10);
  EXPECT_EQ(packet.payload(), buffer().offset(90 * kFormat.bytes_per_frame()));
}

TEST_F(RingBufferTest, ReadNegativeThroughPositiveFrames) {
  const auto packet = ring_buffer().Read(-5, 10);
  EXPECT_EQ(packet.start(), Fixed(-5));
  EXPECT_EQ(packet.length(), 5);
  EXPECT_EQ(packet.payload(), buffer().offset(95 * kFormat.bytes_per_frame()));
}

}  // namespace
}  // namespace media_audio
