// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/consumer_stage.h"

#include <lib/zx/time.h>

#include <map>
#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/mix/simple_packet_queue_producer_stage.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"
#include "src/media/audio/services/mixer/mix/testing/fake_consumer_stage_writer.h"

namespace media_audio {
namespace {

using StartCommand = ConsumerStage::StartCommand;
using StopCommand = ConsumerStage::StopCommand;
using StartedStatus = ConsumerStage::StartedStatus;
using StoppedStatus = ConsumerStage::StoppedStatus;
using ::fuchsia_audio_mixer::PipelineDirection;
using ::fuchsia_mediastreams::wire::AudioSampleFormat;
using ::testing::ElementsAre;
using ::testing::FieldsAre;
using ::testing::VariantWith;

const Format kFormat = Format::CreateOrDie({AudioSampleFormat::kFloat, 2, 48000});

struct TestHarness {
  std::shared_ptr<ConsumerStage> consumer;
  std::shared_ptr<ConsumerStage::CommandQueue> command_queue;
  std::shared_ptr<FakeConsumerStageWriter> writer;
  std::shared_ptr<SimplePacketQueueProducerStage> packet_queue;
};

TestHarness MakeTestHarness(zx::duration presentation_delay,
                            PipelineDirection pipeline_direction = PipelineDirection::kOutput) {
  TestHarness h;
  h.packet_queue =
      std::make_shared<SimplePacketQueueProducerStage>(SimplePacketQueueProducerStage::Args{
          .format = kFormat,
          .reference_clock_koid = DefaultClockKoid(),
      });
  h.command_queue = std::make_shared<ConsumerStage::CommandQueue>();
  h.writer = std::make_shared<FakeConsumerStageWriter>();
  h.consumer = std::make_shared<ConsumerStage>(ConsumerStage::Args{
      .pipeline_direction = pipeline_direction,
      .presentation_delay = presentation_delay,
      .format = kFormat,
      .reference_clock_koid = DefaultClockKoid(),
      .command_queue = h.command_queue,
      .writer = h.writer,
  });
  h.consumer->AddSource(h.packet_queue, {});
  return h;
}

std::shared_ptr<std::vector<float>> PushPacket(TestHarness& h, Fixed start_frame, int64_t length) {
  auto payload = std::make_shared<std::vector<float>>(length * kFormat.channels());
  h.packet_queue->push(PacketView({kFormat, start_frame, length, payload->data()}));
  return payload;
}

TEST(ConsumerStageTest, SourceIsEmpty) {
  auto h = MakeTestHarness(/*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(1);
  const auto pt0 = zx::time(0) + period;

  h.command_queue->push(StartCommand{.start_presentation_time = pt0, .start_frame = 0});

  auto status = h.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
  EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));
  EXPECT_THAT(h.writer->packets(), ElementsAre(FieldsAre(true, 0, 48, nullptr)));
}

TEST(ConsumerStageTest, SourceHasPackets) {
  auto h = MakeTestHarness(/*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(5);
  const auto pt0 = zx::time(0) + period;

  auto payload0 = PushPacket(h, Fixed(0), 48);
  auto payload1 = PushPacket(h, Fixed(48), 48);
  auto payload3 = PushPacket(h, Fixed(144), 48);
  h.command_queue->push(StartCommand{.start_presentation_time = pt0, .start_frame = 0});

  auto status = h.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
  EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));

  // Should have the above three packets with silence at packets 2 and 4.
  EXPECT_THAT(h.writer->packets(), ElementsAre(FieldsAre(false, 0, 48, payload0->data()),    //
                                               FieldsAre(false, 48, 48, payload1->data()),   //
                                               FieldsAre(true, 96, 48, nullptr),             //
                                               FieldsAre(false, 144, 48, payload3->data()),  //
                                               FieldsAre(true, 192, 48, nullptr)));
}

TEST(ConsumerStageTest, SourceHasPacketAtFractionalOffset) {
  auto h = MakeTestHarness(/*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(1);
  const auto pt0 = zx::time(0) + period;

  auto payload0 = PushPacket(h, Fixed(10) + ffl::FromRatio(1, 4), 5);
  auto payload1 = PushPacket(h, Fixed(16), 32);
  h.command_queue->push(StartCommand{.start_presentation_time = pt0, .start_frame = 0});

  auto status = h.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
  EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));

  // The first frame of packet0 overlaps frame 11.
  EXPECT_THAT(h.writer->packets(), ElementsAre(FieldsAre(true, 0, 11, nullptr),            //
                                               FieldsAre(false, 11, 5, payload0->data()),  //
                                               FieldsAre(false, 16, 32, payload1->data())));
}

TEST(ConsumerStageTest, StartDuringJob) {
  auto h = MakeTestHarness(/*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(2);
  const auto pt0 = zx::time(0) + period;

  auto payload0 = PushPacket(h, Fixed(0), 48);
  auto payload1 = PushPacket(h, Fixed(48), 48);
  h.command_queue->push(
      StartCommand{.start_presentation_time = pt0 + zx::msec(1), .start_frame = 48});

  // We start at the second packet.
  auto status = h.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
  EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));
  EXPECT_THAT(h.writer->packets(), ElementsAre(FieldsAre(false, 48, 48, payload1->data())));
}

TEST(ConsumerStageTest, StartAtEndOfJob) {
  auto h = MakeTestHarness(/*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(1);
  const auto pt0 = zx::time(0) + period;

  auto payload0 = PushPacket(h, Fixed(0), 48);
  h.command_queue->push(
      StartCommand{.start_presentation_time = pt0 + zx::msec(1), .start_frame = 48});

  auto status = h.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
  EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));
  EXPECT_THAT(h.writer->packets(), ElementsAre());
}

TEST(ConsumerStageTest, StartAfterJob) {
  auto h = MakeTestHarness(/*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(1);
  const auto pt0 = zx::time(0) + period;

  auto payload0 = PushPacket(h, Fixed(0), 48);
  h.command_queue->push(
      StartCommand{.start_presentation_time = pt0 + zx::msec(3), .start_frame = 144});

  auto status = h.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
  EXPECT_THAT(status, VariantWith<StoppedStatus>(FieldsAre(zx::time(0) + zx::msec(3))));
  EXPECT_THAT(h.writer->packets(), ElementsAre());
}

TEST(ConsumerStageTest, StopDuringJob) {
  auto h = MakeTestHarness(/*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(2);
  const auto pt0 = zx::time(0) + period;

  auto payload0 = PushPacket(h, Fixed(0), 48);
  auto payload1 = PushPacket(h, Fixed(48), 48);
  h.command_queue->push(StartCommand{.start_presentation_time = pt0, .start_frame = 0});
  h.command_queue->push(StopCommand{.stop_frame = 48});

  auto status = h.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
  EXPECT_THAT(status, VariantWith<StoppedStatus>(FieldsAre(std::nullopt)));

  // Since we stop at frame 48 (1ms), there should be silence after packet0.
  EXPECT_THAT(h.writer->packets(), ElementsAre(FieldsAre(false, 0, 48, payload0->data())));
}

TEST(ConsumerStageTest, StopAtEndOfJob) {
  auto h = MakeTestHarness(/*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(1);
  const auto pt0 = zx::time(0) + period;

  auto payload0 = PushPacket(h, Fixed(0), 48);
  h.command_queue->push(StartCommand{.start_presentation_time = pt0, .start_frame = 0});
  h.command_queue->push(StopCommand{.stop_frame = 48});

  auto status = h.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
  EXPECT_TRUE(std::holds_alternative<StoppedStatus>(status));
  EXPECT_THAT(h.writer->packets(), ElementsAre(FieldsAre(false, 0, 48, payload0->data())));
}

TEST(ConsumerStageTest, StopAfterJob) {
  auto h = MakeTestHarness(/*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(1);
  const auto pt0 = zx::time(0) + period;

  auto payload0 = PushPacket(h, Fixed(0), 48);
  h.command_queue->push(StartCommand{.start_presentation_time = pt0, .start_frame = 0});
  h.command_queue->push(StopCommand{.stop_frame = 96});

  auto status = h.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
  EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));
  EXPECT_THAT(h.writer->packets(), ElementsAre(FieldsAre(false, 0, 48, payload0->data())));
}

TEST(ConsumerStageTest, StartAndStopDuringJob) {
  auto h = MakeTestHarness(/*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(3);
  const auto pt0 = zx::time(0) + period;

  // Start at packet1 and stop at packet2.
  auto payload0 = PushPacket(h, Fixed(0), 48);
  auto payload1 = PushPacket(h, Fixed(48), 48);
  auto payload2 = PushPacket(h, Fixed(96), 48);
  h.command_queue->push(
      StartCommand{.start_presentation_time = pt0 + zx::msec(1), .start_frame = 48});
  h.command_queue->push(StopCommand{.stop_frame = 96});

  auto status = h.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
  EXPECT_THAT(status, VariantWith<StoppedStatus>(FieldsAre(std::nullopt)));
  EXPECT_THAT(h.writer->packets(), ElementsAre(FieldsAre(false, 48, 48, payload1->data())));
}

TEST(ConsumerStageTest, StopInSecondJub) {
  auto h = MakeTestHarness(/*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(1);
  const auto pt0 = zx::time(0) + period;

  // Start at packet0 and stop within packet1.
  auto payload0 = PushPacket(h, Fixed(0), 48);
  auto payload1 = PushPacket(h, Fixed(48), 48);
  h.command_queue->push(StartCommand{.start_presentation_time = pt0, .start_frame = 0});
  h.command_queue->push(StopCommand{.stop_frame = 50});

  {
    auto status = h.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
    EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));
    EXPECT_THAT(h.writer->packets(), ElementsAre(FieldsAre(false, 0, 48, payload0->data())));
    h.writer->packets().clear();
  }

  {
    auto status = h.consumer->RunMixJob(DefaultCtx(), zx::time(0) + period, period);
    EXPECT_THAT(status, VariantWith<StoppedStatus>(FieldsAre(std::nullopt)));
    EXPECT_THAT(h.writer->packets(), ElementsAre(FieldsAre(false, 48, 2, payload1->data())));
  }
}

TEST(ConsumerStageTest, StopStartCallbacks) {
  auto h = MakeTestHarness(/*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(1);
  const auto pt0 = zx::time(0) + period;

  bool start_done = false;
  bool stop_done = false;

  // Start at the beginning of the first mix job.
  h.command_queue->push(StartCommand{
      .start_presentation_time = pt0,
      .start_frame = 0,
      .callback = [&start_done]() { start_done = true; },
  });
  // Stop during the second mix job.
  h.command_queue->push(StopCommand{
      .stop_frame = 50,
      .callback = [&stop_done]() { stop_done = true; },
  });

  {
    auto status = h.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
    EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));
    EXPECT_TRUE(start_done);
    EXPECT_FALSE(stop_done);
  }

  {
    auto status = h.consumer->RunMixJob(DefaultCtx(), zx::time(0) + period, period);
    EXPECT_TRUE(std::holds_alternative<StoppedStatus>(status));
    EXPECT_TRUE(start_done);
    EXPECT_TRUE(stop_done);
  }
}

TEST(ConsumerStageTest, RemoveSource) {
  auto h = MakeTestHarness(/*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(1);
  const auto pt0 = zx::time(0) + period;

  auto payload0 = PushPacket(h, Fixed(0), 48);
  auto payload1 = PushPacket(h, Fixed(48), 48);
  h.command_queue->push(StartCommand{.start_presentation_time = pt0, .start_frame = 0});
  h.consumer->RemoveSource(h.packet_queue);

  auto status = h.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
  EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));
  EXPECT_THAT(h.writer->packets(), ElementsAre(FieldsAre(true, 0, 48, nullptr)));
}

TEST(ConsumerStageTest, OutputPipelineWithDelay) {
  auto h = MakeTestHarness(/*presentation_delay=*/zx::msec(1),
                           fuchsia_audio_mixer::PipelineDirection::kOutput);

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(1);
  const auto pt0 = zx::time(0) + period + zx::msec(1);

  auto payload0 = PushPacket(h, Fixed(0), 48);
  h.command_queue->push(StartCommand{.start_presentation_time = pt0, .start_frame = 0});
  auto status = h.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
  EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));
  EXPECT_THAT(h.writer->packets(), ElementsAre(FieldsAre(false, 0, 48, payload0->data())));
}

TEST(ConsumerStageTest, InputPipelineWithDelay) {
  auto h = MakeTestHarness(/*presentation_delay=*/zx::msec(1),
                           fuchsia_audio_mixer::PipelineDirection::kInput);

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(1);
  const auto pt0 = zx::time(0) - period - zx::msec(1);

  auto payload0 = PushPacket(h, Fixed(0), 48);
  h.command_queue->push(StartCommand{.start_presentation_time = pt0, .start_frame = 0});
  auto status = h.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
  EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));
  EXPECT_THAT(h.writer->packets(), ElementsAre(FieldsAre(false, 0, 48, payload0->data())));
}

}  // namespace
}  // namespace media_audio
