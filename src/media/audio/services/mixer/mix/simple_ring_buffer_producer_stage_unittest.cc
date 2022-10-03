// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/simple_ring_buffer_producer_stage.h"

#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/time.h>

#include <optional>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/packet_view.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"

namespace media_audio {
namespace {

using ::fuchsia_audio::SampleType;

const Format kFormat = Format::CreateOrDie({SampleType::kFloat32, 2, 48000});
constexpr int64_t kRingBufferFrames = 100;

TEST(SimpleRingBufferProducerStageTest, Read) {
  auto buffer =
      MemoryMappedBuffer::CreateOrDie(kRingBufferFrames * kFormat.bytes_per_frame(), true);
  auto ring_buffer = RingBuffer::Create({
      .format = kFormat,
      .reference_clock = DefaultUnreadableClock(),
      .buffer = buffer,
      .producer_frames = kRingBufferFrames / 2,
      .consumer_frames = kRingBufferFrames / 2,
  });

  // Create a producer and start it.
  SimpleRingBufferProducerStage producer("producer", ring_buffer);
  producer.UpdatePresentationTimeToFracFrame(DefaultPresentationTimeToFracFrame(kFormat));

  // Test a few simple cases. Since ReadImpl delegates the heavy-lifting to RingBuffer::Read, we
  // don't need to test this exhaustively.
  {
    const auto packet = producer.Read(DefaultCtx(), Fixed(0), 10);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start(), Fixed(0));
    EXPECT_EQ(packet->length(), 10);
    EXPECT_EQ(packet->payload(), buffer->offset(0));
  }

  {
    const auto packet = producer.Read(DefaultCtx(), Fixed(95), 10);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start(), Fixed(95));
    EXPECT_EQ(packet->length(), 5);
    EXPECT_EQ(packet->payload(), buffer->offset(95 * kFormat.bytes_per_frame()));
  }

  {
    const auto packet = producer.Read(DefaultCtx(), Fixed(100), 5);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start(), Fixed(100));
    EXPECT_EQ(packet->length(), 5);
    EXPECT_EQ(packet->payload(), buffer->offset(0));
  }
}

}  // namespace
}  // namespace media_audio
