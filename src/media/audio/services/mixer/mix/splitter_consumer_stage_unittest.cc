// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/splitter_consumer_stage.h"

#include <lib/zx/time.h>

#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/common/memory_mapped_buffer.h"
#include "src/media/audio/services/mixer/mix/simple_packet_queue_producer_stage.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"
#include "src/media/audio/services/mixer/mix/testing/fake_pipeline_thread.h"

namespace media_audio {
namespace {

using ::testing::ElementsAre;

const Format kFormat = Format::CreateOrDie({fuchsia_audio::SampleType::kInt32, 1, 1000});
const auto kRingBufferFrames = 30;
const auto kRingBufferSize = kRingBufferFrames * kFormat.bytes_per_frame();

class SplitterConsumerStageTest : public ::testing::Test {
 public:
  SplitterConsumerStageTest() {
    packet_queue_ = MakeDefaultPacketQueue(kFormat);
    packet_queue_->set_thread(thread_);

    ring_buffer_ = std::make_shared<RingBuffer>(
        kFormat, DefaultUnreadableClock(), MemoryMappedBuffer::CreateOrDie(kRingBufferSize, true));

    consumer_ = std::make_shared<SplitterConsumerStage>(SplitterConsumerStage::Args{
        .format = kFormat,
        .reference_clock = DefaultUnreadableClock(),
        .thread = thread_,
        .ring_buffer = ring_buffer_,
    });
    consumer_->AddSource(packet_queue_, {});
  }

  SimplePacketQueueProducerStage& packet_queue() const { return *packet_queue_; }
  RingBuffer& ring_buffer() const { return *ring_buffer_; }
  SplitterConsumerStage& consumer() const { return *consumer_; }

 private:
  std::shared_ptr<FakePipelineThread> thread_ = std::make_shared<FakePipelineThread>(1);
  std::shared_ptr<SimplePacketQueueProducerStage> packet_queue_;
  std::shared_ptr<RingBuffer> ring_buffer_;
  std::shared_ptr<SplitterConsumerStage> consumer_;
};

TEST_F(SplitterConsumerStageTest, UpdatePresentationTimeToFracFrame) {
  // no-op
  consumer().UpdatePresentationTimeToFracFrame(std::nullopt);
  EXPECT_EQ(consumer().presentation_time_to_frac_frame(), std::nullopt);
  EXPECT_EQ(packet_queue().presentation_time_to_frac_frame(), std::nullopt);

  // First non-nullopt function takes effect.
  const auto expected = TimelineFunction(0, 0, 1, 1);
  consumer().UpdatePresentationTimeToFracFrame(expected);
  EXPECT_EQ(consumer().presentation_time_to_frac_frame(), expected);
  EXPECT_EQ(packet_queue().presentation_time_to_frac_frame(), expected);

  // Subsequent calls have no effect.
  consumer().UpdatePresentationTimeToFracFrame(TimelineFunction(1, 1, 2, 1));
  EXPECT_EQ(consumer().presentation_time_to_frac_frame(), expected);
  EXPECT_EQ(packet_queue().presentation_time_to_frac_frame(), expected);

  consumer().UpdatePresentationTimeToFracFrame(std::nullopt);
  EXPECT_EQ(consumer().presentation_time_to_frac_frame(), expected);
  EXPECT_EQ(packet_queue().presentation_time_to_frac_frame(), expected);
}

TEST_F(SplitterConsumerStageTest, FillBuffer) {
  // Our frame rate is 1kHz, so an 8ms delay is 8 frames.
  ScopedThreadChecker checker(consumer().thread()->checker());
  consumer().set_max_downstream_output_pipeline_delay(zx::msec(8));

  // On this timeline, frame 0 is presented at time 0.
  consumer().UpdatePresentationTimeToFracFrame(
      TimelineFunction(0, 0, kFormat.frac_frames_per_ns()));

  // The tests will write 10 frames. Give the source 11 to make sure we stop after 10.
  std::vector<int32_t> payload(11);
  packet_queue().push(PacketView({kFormat, Fixed(0), 11, payload.data()}));
  for (int32_t k = 0; k < 11; k++) {
    payload[k] = k;
  }

  const auto t0 = zx::time(0);
  const auto t2 = zx::time(0) + zx::msec(2);

  // First call, starting from t=0, should fill up to t=max_downstream_output_pipeline_delay=8.
  // Do this twice to make sure it is idempotent.
  for (auto k = 0; k < 2; k++) {
    SCOPED_TRACE("iter" + std::to_string(k));

    MixJobContext ctx(DefaultClockSnapshots(), t0, /* unused deadline */ t0 + zx::nsec(1));
    consumer().FillBuffer(ctx);
    EXPECT_EQ(consumer().end_of_last_fill(), 8);

    auto packet = ring_buffer().Read(0, 11);
    ASSERT_EQ(packet.start_frame(), 0);
    ASSERT_EQ(packet.end_frame(), 11);

    // Last two samples are not filled in yet.
    std::vector<int32_t> samples(static_cast<int32_t*>(packet.payload()),
                                 static_cast<int32_t*>(packet.payload()) + 11);
    EXPECT_THAT(samples, ElementsAre(0, 1, 2, 3, 4, 5, 6, 7, 0, 0, 0));
  }

  // Update the payload to check which frames are written by the next test.
  for (int32_t k = 0; k < 11; k++) {
    payload[k] = 100 + k;
  }

  // Next call, starting from t=2, should fill up to t=10.
  // Do this twice to make sure it is idempotent.
  for (auto k = 0; k < 2; k++) {
    MixJobContext ctx(DefaultClockSnapshots(), t2, /* unused deadline */ t2 + zx::nsec(1));
    consumer().FillBuffer(ctx);
    EXPECT_EQ(consumer().end_of_last_fill(), 10);

    auto packet = ring_buffer().Read(0, 11);
    ASSERT_EQ(packet.start_frame(), 0);
    ASSERT_EQ(packet.end_frame(), 11);

    // Samples 8 and 9 are added.
    std::vector<int32_t> samples(static_cast<int32_t*>(packet.payload()),
                                 static_cast<int32_t*>(packet.payload()) + 11);
    EXPECT_THAT(samples, ElementsAre(0, 1, 2, 3, 4, 5, 6, 7, 108, 109, 0));
  }
}

}  // namespace
}  // namespace media_audio
