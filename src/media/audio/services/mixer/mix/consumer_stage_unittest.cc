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
#include "src/media/audio/services/mixer/mix/testing/consumer_stage_wrapper.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"

namespace media_audio {
namespace {

using StartCommand = ConsumerStage::StartCommand;
using StopCommand = ConsumerStage::StopCommand;
using StartedStatus = ConsumerStage::StartedStatus;
using StoppedStatus = ConsumerStage::StoppedStatus;
using RealTime = StartStopControl::RealTime;
using WhichClock = StartStopControl::WhichClock;
using ::fuchsia_audio::SampleType;
using ::testing::ElementsAre;
using ::testing::FieldsAre;
using ::testing::VariantWith;

const Format kFormat = Format::CreateOrDie({SampleType::kFloat32, 2, 48000});

TEST(ConsumerStageTest, SourceIsEmpty) {
  ConsumerStageWrapper w(kFormat, /*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(1);
  const auto pt0 = zx::time(0) + period;

  w.pending_start_stop_command->set_must_be_empty(StartCommand{
      .start_time = RealTime{.clock = WhichClock::kReference, .time = pt0},
      .start_position = Fixed(0),
  });

  auto status = w.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
  EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));
  EXPECT_THAT(w.writer->packets(), ElementsAre(FieldsAre(true, 0, 48, nullptr)));
  EXPECT_THAT(w.writer->end_calls(), ElementsAre());
}

TEST(ConsumerStageTest, SourceHasPackets) {
  ConsumerStageWrapper w(kFormat, /*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(5);
  const auto pt0 = zx::time(0) + period;

  auto payload0 = w.PushPacket(Fixed(0), 48);
  auto payload1 = w.PushPacket(Fixed(48), 48);
  auto payload3 = w.PushPacket(Fixed(144), 48);
  w.pending_start_stop_command->set_must_be_empty(StartCommand{
      .start_time = RealTime{.clock = WhichClock::kReference, .time = pt0},
      .start_position = Fixed(0),
  });

  auto status = w.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
  EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));

  // Should have the above three packets with silence at packets 2 and 4.
  EXPECT_THAT(w.writer->packets(), ElementsAre(FieldsAre(false, 0, 48, payload0->data()),    //
                                               FieldsAre(false, 48, 48, payload1->data()),   //
                                               FieldsAre(true, 96, 48, nullptr),             //
                                               FieldsAre(false, 144, 48, payload3->data()),  //
                                               FieldsAre(true, 192, 48, nullptr)));
  EXPECT_THAT(w.writer->end_calls(), ElementsAre());
}

TEST(ConsumerStageTest, SourceHasPacketAtFractionalOffset) {
  ConsumerStageWrapper w(kFormat, /*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(1);
  const auto pt0 = zx::time(0) + period;

  auto payload0 = w.PushPacket(Fixed(10) + ffl::FromRatio(1, 4), 5);
  auto payload1 = w.PushPacket(Fixed(16), 32);
  w.pending_start_stop_command->set_must_be_empty(StartCommand{
      .start_time = RealTime{.clock = WhichClock::kReference, .time = pt0},
      .start_position = Fixed(0),
  });

  auto status = w.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
  EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));

  // The first frame of packet0 overlaps frame 11.
  EXPECT_THAT(w.writer->packets(), ElementsAre(FieldsAre(true, 0, 11, nullptr),            //
                                               FieldsAre(false, 11, 5, payload0->data()),  //
                                               FieldsAre(false, 16, 32, payload1->data())));
  EXPECT_THAT(w.writer->end_calls(), ElementsAre());
}

TEST(ConsumerStageTest, StartDuringJob) {
  ConsumerStageWrapper w(kFormat, /*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(2);
  const auto pt0 = zx::time(0) + period;

  auto payload0 = w.PushPacket(Fixed(0), 48);
  auto payload1 = w.PushPacket(Fixed(48), 48);
  w.pending_start_stop_command->set_must_be_empty(StartCommand{
      .start_time = RealTime{.clock = WhichClock::kReference, .time = pt0 + zx::msec(1)},
      .start_position = Fixed(48),
  });

  // We start at the second packet.
  auto status = w.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
  EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));
  EXPECT_THAT(w.writer->packets(), ElementsAre(FieldsAre(false, 48, 48, payload1->data())));
  EXPECT_THAT(w.writer->end_calls(), ElementsAre());
}

TEST(ConsumerStageTest, StartAtEndOfJob) {
  ConsumerStageWrapper w(kFormat, /*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(1);
  const auto pt0 = zx::time(0) + period;

  auto payload0 = w.PushPacket(Fixed(0), 48);
  w.pending_start_stop_command->set_must_be_empty(StartCommand{
      .start_time = RealTime{.clock = WhichClock::kReference, .time = pt0 + zx::msec(1)},
      .start_position = Fixed(48),
  });

  auto status = w.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
  EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));
  EXPECT_THAT(w.writer->packets(), ElementsAre());
  EXPECT_THAT(w.writer->end_calls(), ElementsAre());
}

TEST(ConsumerStageTest, StartAfterJob) {
  ConsumerStageWrapper w(kFormat, /*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(1);
  const auto pt0 = zx::time(0) + period;

  auto payload0 = w.PushPacket(Fixed(0), 48);
  w.pending_start_stop_command->set_must_be_empty(StartCommand{
      .start_time = RealTime{.clock = WhichClock::kReference, .time = pt0 + zx::msec(3)},
      .start_position = Fixed(144),
  });

  auto status = w.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
  EXPECT_THAT(status, VariantWith<StoppedStatus>(FieldsAre(zx::time(0) + zx::msec(3))));
  EXPECT_THAT(w.writer->packets(), ElementsAre());
  EXPECT_THAT(w.writer->end_calls(), ElementsAre());
}

TEST(ConsumerStageTest, StopDuringJob) {
  ConsumerStageWrapper w(kFormat, /*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(2);
  const auto pt0 = zx::time(0) + period;

  auto payload0 = w.PushPacket(Fixed(0), 48);
  auto payload1 = w.PushPacket(Fixed(48), 48);

  // Mix job ends at a StartCommand.
  w.pending_start_stop_command->set_must_be_empty(StartCommand{
      .start_time = RealTime{.clock = WhichClock::kReference, .time = pt0},
      .start_position = Fixed(0),
  });
  {
    auto status = w.consumer->RunMixJob(DefaultCtx(), zx::time(0) - period, period);
    EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));
  }

  // Mix job with a StopCommand in the middle.
  w.pending_start_stop_command->set_must_be_empty(StopCommand{.when = Fixed(48)});
  {
    auto status = w.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
    EXPECT_THAT(status, VariantWith<StoppedStatus>(FieldsAre(std::nullopt)));
  }

  // Since we stop at frame 48 (1ms), there should be silence after packet0.
  EXPECT_THAT(w.writer->packets(), ElementsAre(FieldsAre(false, 0, 48, payload0->data())));
  EXPECT_THAT(w.writer->end_calls(), ElementsAre(48));
}

TEST(ConsumerStageTest, StopAtEndOfJob) {
  ConsumerStageWrapper w(kFormat, /*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(1);
  const auto pt0 = zx::time(0) + period;

  auto payload0 = w.PushPacket(Fixed(0), 48);

  // Mix job ends at a StartCommand.
  w.pending_start_stop_command->set_must_be_empty(StartCommand{
      .start_time = RealTime{.clock = WhichClock::kReference, .time = pt0},
      .start_position = Fixed(0),
  });
  {
    auto status = w.consumer->RunMixJob(DefaultCtx(), zx::time(0) - period, period);
    EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));
  }

  // Mix job with a StopCommand at the end.
  w.pending_start_stop_command->set_must_be_empty(StopCommand{.when = Fixed(48)});
  {
    auto status = w.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
    EXPECT_TRUE(std::holds_alternative<StoppedStatus>(status));
  }

  EXPECT_THAT(w.writer->packets(), ElementsAre(FieldsAre(false, 0, 48, payload0->data())));
  EXPECT_THAT(w.writer->end_calls(), ElementsAre(48));
}

TEST(ConsumerStageTest, StopAfterJob) {
  ConsumerStageWrapper w(kFormat, /*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(1);
  const auto pt0 = zx::time(0) + period;

  auto payload0 = w.PushPacket(Fixed(0), 48);

  // Mix job ends at a StartCommand.
  w.pending_start_stop_command->set_must_be_empty(StartCommand{
      .start_time = RealTime{.clock = WhichClock::kReference, .time = pt0},
      .start_position = Fixed(0),
  });
  {
    auto status = w.consumer->RunMixJob(DefaultCtx(), zx::time(0) - period, period);
    EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));
  }

  // Mix job with a StopCommand in the middle.
  w.pending_start_stop_command->set_must_be_empty(StopCommand{.when = Fixed(96)});
  {
    auto status = w.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
    EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));
  }

  EXPECT_THAT(w.writer->packets(), ElementsAre(FieldsAre(false, 0, 48, payload0->data())));
  EXPECT_THAT(w.writer->end_calls(), ElementsAre());
}

TEST(ConsumerStageTest, StopInSecondJob) {
  ConsumerStageWrapper w(kFormat, /*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(1);
  const auto pt0 = zx::time(0) + period;

  // Start at packet0 and stop within packet1.
  auto payload0 = w.PushPacket(Fixed(0), 48);
  auto payload1 = w.PushPacket(Fixed(48), 48);

  // Mix job ends at a StartCommand.
  w.pending_start_stop_command->set_must_be_empty(StartCommand{
      .start_time = RealTime{.clock = WhichClock::kReference, .time = pt0},
      .start_position = Fixed(0),
  });
  {
    auto status = w.consumer->RunMixJob(DefaultCtx(), zx::time(0) - period, period);
    EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));
  }

  w.pending_start_stop_command->set_must_be_empty(StopCommand{.when = Fixed(50)});

  // Mix job ends before the StopCommand.
  {
    auto status = w.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
    EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));
    EXPECT_THAT(w.writer->packets(), ElementsAre(FieldsAre(false, 0, 48, payload0->data())));
    EXPECT_THAT(w.writer->end_calls(), ElementsAre());
    w.writer->packets().clear();
  }

  // Mix job crosses the StopCommand.
  {
    auto status = w.consumer->RunMixJob(DefaultCtx(), zx::time(0) + period, period);
    EXPECT_THAT(status, VariantWith<StoppedStatus>(FieldsAre(std::nullopt)));
    EXPECT_THAT(w.writer->packets(), ElementsAre(FieldsAre(false, 48, 2, payload1->data())));
    EXPECT_THAT(w.writer->end_calls(), ElementsAre(50));
  }
}

TEST(ConsumerStageTest, RemoveSource) {
  ConsumerStageWrapper w(kFormat, /*presentation_delay=*/zx::nsec(0));

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(1);
  const auto pt0 = zx::time(0) + period;

  auto payload0 = w.PushPacket(Fixed(0), 48);
  auto payload1 = w.PushPacket(Fixed(48), 48);
  w.pending_start_stop_command->set_must_be_empty(StartCommand{
      .start_time = RealTime{.clock = WhichClock::kReference, .time = pt0},
      .start_position = Fixed(0),
  });
  w.consumer->RemoveSource(w.packet_queue);

  auto status = w.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
  EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));
  EXPECT_THAT(w.writer->packets(), ElementsAre(FieldsAre(true, 0, 48, nullptr)));
  EXPECT_THAT(w.writer->end_calls(), ElementsAre());
}

TEST(ConsumerStageTest, OutputPipelineWithDelay) {
  ConsumerStageWrapper w(kFormat, /*presentation_delay=*/zx::msec(1), PipelineDirection::kOutput);

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(1);
  const auto pt0 = zx::time(0) + period + zx::msec(1);

  auto payload0 = w.PushPacket(Fixed(0), 48);
  w.pending_start_stop_command->set_must_be_empty(StartCommand{
      .start_time = RealTime{.clock = WhichClock::kReference, .time = pt0},
      .start_position = Fixed(0),
  });
  auto status = w.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
  EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));
  EXPECT_THAT(w.writer->packets(), ElementsAre(FieldsAre(false, 0, 48, payload0->data())));
  EXPECT_THAT(w.writer->end_calls(), ElementsAre());
}

TEST(ConsumerStageTest, InputPipelineWithDelay) {
  ConsumerStageWrapper w(kFormat, /*presentation_delay=*/zx::msec(1), PipelineDirection::kInput);

  // pt0 is the presentation time consumed by RunMixJob(ctx, 0, period). Since we consume one
  // period ahead, this is start of the second mix period.
  const auto period = zx::msec(1);
  const auto pt0 = zx::time(0) - period - zx::msec(1);

  auto payload0 = w.PushPacket(Fixed(0), 48);
  w.pending_start_stop_command->set_must_be_empty(StartCommand{
      .start_time = RealTime{.clock = WhichClock::kReference, .time = pt0},
      .start_position = Fixed(0),
  });
  auto status = w.consumer->RunMixJob(DefaultCtx(), zx::time(0), period);
  EXPECT_TRUE(std::holds_alternative<StartedStatus>(status));
  EXPECT_THAT(w.writer->packets(), ElementsAre(FieldsAre(false, 0, 48, payload0->data())));
  EXPECT_THAT(w.writer->end_calls(), ElementsAre());
}

}  // namespace
}  // namespace media_audio
