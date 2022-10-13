// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/ring_buffer.h"

#include <lib/zx/time.h>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/common/memory_mapped_buffer.h"
#include "src/media/audio/services/mixer/mix/packet_view.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"

namespace media_audio {
namespace {

using ::fuchsia_audio::SampleType;
using ::testing::ElementsAre;

const Format kFormat = Format::CreateOrDie({SampleType::kFloat32, 2, 48000});
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
  std::shared_ptr<RingBuffer> ring_buffer_ = std::make_shared<RingBuffer>(
      kFormat, DefaultUnreadableClock(),
      std::make_shared<RingBuffer::Buffer>(buffer_,
                                           /*producer_frames=*/kRingBufferFrames / 2,
                                           /*consumer_frames=*/kRingBufferFrames / 2));
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

TEST(RingBufferUpdateTest, SetBufferAsync) {
  const Format kFormat = Format::CreateOrDie({SampleType::kInt32, 1, 48000});
  const auto buffer0 = MemoryMappedBuffer::CreateOrDie(10 * kFormat.bytes_per_frame(), true);
  const auto buffer1 = MemoryMappedBuffer::CreateOrDie(14 * kFormat.bytes_per_frame(), true);
  const auto buffer2 = MemoryMappedBuffer::CreateOrDie(4 * kFormat.bytes_per_frame(), true);
  const auto buffer3 = MemoryMappedBuffer::CreateOrDie(4 * kFormat.bytes_per_frame(), true);

  const auto ring_buffer = std::make_shared<RingBuffer>(
      kFormat, DefaultUnreadableClock(), std::make_shared<RingBuffer::Buffer>(buffer0, 5, 5));

  // Fill buffer0 with known values.
  for (int k = 0; k < 10; k++) {
    auto samples = static_cast<int32_t*>(buffer0->start());
    samples[k] = k;
  }

  // Switch to buffer1 at frame 31. This should copy frames 21-30, using the following copies:
  //
  // buffer0[1..7] => buffer1[7..13]    // frames [21,27]
  // buffer0[8..9] => buffer1[0..1]     // frames [28,29]
  // buffer0[0]    => buffer1[2]        // frames [30]
  {
    ring_buffer->SetBufferAsync(std::make_shared<RingBuffer::Buffer>(buffer1, 7, 7));
    [[maybe_unused]] auto packet = ring_buffer->PrepareToWrite(31, 3);

    std::vector<int32_t> samples(static_cast<int32_t*>(buffer1->start()),
                                 static_cast<int32_t*>(buffer1->start()) + 14);
    EXPECT_THAT(samples, ElementsAre(8, 9, 0,                // [0,2]
                                     0, 0, 0, 0,             // [3,6]
                                     1, 2, 3, 4, 5, 6, 7));  // [7,13]
  }

  // Fill buffer1 with known values to simplify the following test.
  for (int k = 0; k < 14; k++) {
    auto samples = static_cast<int32_t*>(buffer1->start());
    samples[k] = k;
  }

  // Switch to buffer2, then immediately switch to buffer3 before calling PrepareToWrite at frame
  // 34. This should copy frames 30-33 to buffer3, using the following copies:
  //
  // buffer1[2,3] => buffer3[2,3]   // frames [30,31]
  // buffer1[4,5] => buffer3[0,1]   // frames [32,33]
  {
    ring_buffer->SetBufferAsync(std::make_shared<RingBuffer::Buffer>(buffer2, 2, 2));
    ring_buffer->SetBufferAsync(std::make_shared<RingBuffer::Buffer>(buffer3, 2, 2));
    [[maybe_unused]] auto packet = ring_buffer->PrepareToWrite(34, 2);

    std::vector<int32_t> samples(static_cast<int32_t*>(buffer3->start()),
                                 static_cast<int32_t*>(buffer3->start()) + 4);
    EXPECT_THAT(samples, ElementsAre(4, 5, 2, 3));
  }
}

}  // namespace
}  // namespace media_audio
