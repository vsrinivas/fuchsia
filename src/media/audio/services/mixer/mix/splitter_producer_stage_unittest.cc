// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/splitter_producer_stage.h"

#include <lib/zx/time.h>

#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/common/memory_mapped_buffer.h"
#include "src/media/audio/services/mixer/mix/simple_packet_queue_producer_stage.h"
#include "src/media/audio/services/mixer/mix/splitter_consumer_stage.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"
#include "src/media/audio/services/mixer/mix/testing/fake_pipeline_thread.h"

namespace media_audio {
namespace {

using ::testing::ElementsAre;

const Format kFormat = Format::CreateOrDie({fuchsia_audio::SampleType::kInt32, 1, 1000});
const auto kRingBufferFrames = 30;
const auto kRingBufferSize = kRingBufferFrames * kFormat.bytes_per_frame();

class SplitterProducerStageTest : public ::testing::Test {
 public:
  SplitterProducerStageTest() {
    packet_queue_ = MakeDefaultPacketQueue(kFormat);
    packet_queue_->set_thread(thread_);

    ring_buffer_ = std::make_shared<RingBuffer>(
        kFormat, DefaultUnreadableClock(), MemoryMappedBuffer::CreateOrDie(kRingBufferSize, true));

    consumer_ = std::make_shared<SplitterConsumerStage>(SplitterConsumerStage::Args{
        .format = kFormat,
        .reference_clock = DefaultUnreadableClock(),
        .ring_buffer = ring_buffer_,
    });
    consumer_->AddSource(packet_queue_, {});
    consumer_->set_thread(thread_);
  }

  SimplePacketQueueProducerStage& packet_queue() { return *packet_queue_; }
  RingBuffer& ring_buffer() { return *ring_buffer_; }
  SplitterConsumerStage& consumer() { return *consumer_; }

  // Defaults to the same thread as the consumer.
  std::shared_ptr<SplitterProducerStage> MakeProducer(
      std::shared_ptr<FakePipelineThread> thread = nullptr) {
    auto producer = std::make_shared<SplitterProducerStage>(SplitterProducerStage::Args{
        .format = kFormat,
        .reference_clock = DefaultUnreadableClock(),
        .ring_buffer = ring_buffer_,
        .consumer = consumer_,
    });
    producer->set_thread(thread ? std::move(thread) : thread_);
    return producer;
  }

 private:
  std::shared_ptr<FakePipelineThread> thread_ = std::make_shared<FakePipelineThread>(1);
  std::shared_ptr<SimplePacketQueueProducerStage> packet_queue_;
  std::shared_ptr<RingBuffer> ring_buffer_;
  std::shared_ptr<SplitterConsumerStage> consumer_;
};

TEST_F(SplitterProducerStageTest, UpdatePresentationTimeToFracFrame) {
  auto producer1 = MakeProducer();
  auto producer2 = MakeProducer(std::make_shared<FakePipelineThread>(2));
  const auto expected = TimelineFunction(0, 0, 1, 1);

  // This call should have no effect on the consumer because producer2 runs on a different thread.
  producer2->UpdatePresentationTimeToFracFrame(expected);
  EXPECT_EQ(producer2->presentation_time_to_frac_frame(), expected);
  EXPECT_EQ(consumer().presentation_time_to_frac_frame(), std::nullopt);
  EXPECT_EQ(packet_queue().presentation_time_to_frac_frame(), std::nullopt);

  // This call should take effect on the consumer because producer1 runs on the same thread.
  producer1->UpdatePresentationTimeToFracFrame(expected);
  EXPECT_EQ(producer2->presentation_time_to_frac_frame(), expected);
  EXPECT_EQ(consumer().presentation_time_to_frac_frame(), expected);
  EXPECT_EQ(packet_queue().presentation_time_to_frac_frame(), expected);
}

TEST_F(SplitterProducerStageTest, Advance) {
  MixJobContext ctx(DefaultClockSnapshots(), zx::time(0), /* unused deadline */ zx::time(1));

  // Advancing a producer on another thread does not affect the consumer.
  auto producer1 = MakeProducer(std::make_shared<FakePipelineThread>(2));
  producer1->UpdatePresentationTimeToFracFrame(
      TimelineFunction(Fixed(50).raw_value(), 0, kFormat.frac_frames_per_ns()));
  producer1->Advance(ctx, Fixed(60));
  EXPECT_EQ(packet_queue().next_readable_frame(), std::nullopt);

  // Advancing a producer on the same thread should affect the consumer.
  auto producer2 = MakeProducer();
  producer2->UpdatePresentationTimeToFracFrame(
      TimelineFunction(Fixed(0).raw_value(), 0, kFormat.frac_frames_per_ns()));
  producer2->Advance(ctx, Fixed(10));
  EXPECT_EQ(packet_queue().next_readable_frame(), Fixed(10));

  // Advancing a producer on the same thread should affect the consumer.
  // This producer is offset by +50 frames.
  auto producer3 = MakeProducer();
  producer3->UpdatePresentationTimeToFracFrame(
      TimelineFunction(Fixed(50).raw_value(), 0, kFormat.frac_frames_per_ns()));
  producer3->Advance(ctx, Fixed(70));
  EXPECT_EQ(packet_queue().next_readable_frame(), Fixed(20));
}

TEST_F(SplitterProducerStageTest, Read) {
  // Our frame rate is 1kHz, so a 10ms delay is 10 frames.
  ScopedThreadChecker checker(consumer().thread()->checker());
  consumer().set_max_downstream_delay(zx::msec(10));

  // Feed a packet into the source.
  std::vector<int32_t> payload(20);
  packet_queue().push(PacketView({kFormat, Fixed(0), 20, payload.data()}));
  for (int32_t k = 0; k < 20; k++) {
    payload[k] = k;
  }

  // Simulate running mix jobs on each producer starting at t0.
  const auto t0 = zx::time(0);
  MixJobContext ctx(DefaultClockSnapshots(), t0, /* unused deadline */ t0 + zx::nsec(1));

  // Producers from different threads cannot prime the consumer.
  {
    SCOPED_TRACE("producer0.Read(0, 1): consumer not started yet");
    auto producer0 = MakeProducer(std::make_shared<FakePipelineThread>(99));
    producer0->UpdatePresentationTimeToFracFrame(
        TimelineFunction(0, 0, kFormat.frac_frames_per_ns()));
    auto packet = producer0->Read(ctx, Fixed(0), 1);
    EXPECT_FALSE(packet);
  }

  // producer1: same thread, same timeline as the consumer (frame 0 is presented at time 0).
  // This primes the consumer.
  {
    SCOPED_TRACE("producer1.Read(0, 5)");
    auto producer1 = MakeProducer();
    producer1->UpdatePresentationTimeToFracFrame(
        TimelineFunction(0, 0, kFormat.frac_frames_per_ns()));

    auto packet = producer1->Read(ctx, Fixed(0), 5);
    EXPECT_EQ(packet->start(), Fixed(0));
    EXPECT_EQ(packet->end(), Fixed(5));

    std::vector<int32_t> samples(static_cast<int32_t*>(packet->payload()),
                                 static_cast<int32_t*>(packet->payload()) + 5);
    EXPECT_THAT(samples, ElementsAre(0, 1, 2, 3, 4));
  }

  // producer2: same thread, offset timeline (frame 50 is presented at time 0).
  // Reads what was primed by producer1.
  {
    SCOPED_TRACE("producer2.Read(50, 5)");
    auto producer2 = MakeProducer();
    producer2->UpdatePresentationTimeToFracFrame(
        TimelineFunction(Fixed(50).raw_value(), 0, kFormat.frac_frames_per_ns()));

    auto packet = producer2->Read(ctx, Fixed(50), 5);  // producer2 is offset by 50 frames
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start(), Fixed(50));
    EXPECT_EQ(packet->end(), Fixed(55));

    std::vector<int32_t> samples(static_cast<int32_t*>(packet->payload()),
                                 static_cast<int32_t*>(packet->payload()) + 5);
    EXPECT_THAT(samples, ElementsAre(0, 1, 2, 3, 4));
  }

  // producer3: different thread, same timeline as the consumer (frame 0 is presented at time 0).
  // Reads what was primed by producer1.
  auto producer3 = MakeProducer(std::make_shared<FakePipelineThread>(3));
  {
    SCOPED_TRACE("producer3.Read(0, 4)");
    producer3->UpdatePresentationTimeToFracFrame(
        TimelineFunction(0, 0, kFormat.frac_frames_per_ns()));

    auto packet = producer3->Read(ctx, Fixed(0), 5);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start(), Fixed(0));
    EXPECT_EQ(packet->end(), Fixed(5));

    std::vector<int32_t> samples(static_cast<int32_t*>(packet->payload()),
                                 static_cast<int32_t*>(packet->payload()) + 5);
    EXPECT_THAT(samples, ElementsAre(0, 1, 2, 3, 4));
  }

  // producer4: different thread, offset timeline (frame 50 is presented at time 0).
  // Reads what was primed by producer1.
  {
    SCOPED_TRACE("producer4.Read(50, 5)");
    auto producer4 = MakeProducer(std::make_shared<FakePipelineThread>(4));
    producer4->UpdatePresentationTimeToFracFrame(
        TimelineFunction(Fixed(50).raw_value(), 0, kFormat.frac_frames_per_ns()));

    auto packet = producer4->Read(ctx, Fixed(50), 5);  // producer4 is offset by 50 frames
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start(), Fixed(50));
    EXPECT_EQ(packet->end(), Fixed(55));

    std::vector<int32_t> samples(static_cast<int32_t*>(packet->payload()),
                                 static_cast<int32_t*>(packet->payload()) + 5);
    EXPECT_THAT(samples, ElementsAre(0, 1, 2, 3, 4));
  }

  // The consumer is filled up to frame 10, therefore this should return 5 frames.
  {
    SCOPED_TRACE("producer3.Read(5, 10)");
    auto packet = producer3->Read(ctx, Fixed(5), 10);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start(), Fixed(5));
    EXPECT_EQ(packet->end(), Fixed(10));  // only filled up through frame 10

    std::vector<int32_t> samples(static_cast<int32_t*>(packet->payload()),
                                 static_cast<int32_t*>(packet->payload()) + 5);
    EXPECT_THAT(samples, ElementsAre(5, 6, 7, 8, 9));
  }

  // The consumer is not filled past frame 10, therefore this should return nothing.
  {
    SCOPED_TRACE("producer3.Read(15, 5)");
    auto packet = producer3->Read(ctx, Fixed(15), 5);
    EXPECT_FALSE(packet);
  }
}

}  // namespace
}  // namespace media_audio
