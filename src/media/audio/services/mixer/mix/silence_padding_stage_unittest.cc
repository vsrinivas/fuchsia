// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/silence_padding_stage.h"

#include <memory>
#include <optional>

#include <ffl/string.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/packet_view.h"
#include "src/media/audio/services/mixer/mix/simple_packet_queue_producer_stage.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"

namespace media_audio {
namespace {

using ::fuchsia_audio::SampleType;

const auto kFormat = Format::CreateOrDie({SampleType::kInt16, 1, 48000});

std::shared_ptr<SilencePaddingStage> MakeSilencePaddingStage(
    Fixed silence_frame_count, bool round_down_fractional_frames,
    std::shared_ptr<PipelineStage> source) {
  auto stage = std::make_shared<SilencePaddingStage>(kFormat, DefaultClock(), silence_frame_count,
                                                     round_down_fractional_frames);
  if (source) {
    stage->AddSource(source, /*options=*/{});
  }
  stage->UpdatePresentationTimeToFracFrame(DefaultPresentationTimeToFracFrame(kFormat));
  return stage;
}

std::shared_ptr<SimplePacketQueueProducerStage> MakePacketQueueProducerStage() {
  return MakeDefaultPacketQueue(kFormat);
}

void ExpectNullPacket(const std::optional<PipelineStage::Packet>& packet) {
  EXPECT_FALSE(packet) << ffl::String::DecRational << "start=" << packet->start()
                       << " end=" << packet->end();
}

void ExpectPacket(const std::optional<PipelineStage::Packet>& packet, Fixed want_start,
                  Fixed want_end, int16_t want_sample) {
  ASSERT_TRUE(packet);
  EXPECT_EQ(want_sample, reinterpret_cast<int16_t*>(packet->payload())[0]);
  EXPECT_EQ(want_start, packet->start());
  EXPECT_EQ(want_end, packet->end());
}

TEST(SilencePaddingStageTest, NoSource) {
  auto silence_padding_stage = MakeSilencePaddingStage(Fixed(10),
                                                       /*round_down_fractional_frames=*/true,
                                                       /*source=*/nullptr);

  // Since no source is added, no packet should be returned.
  const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(0), 20);
  ExpectNullPacket(packet);
}

TEST(SilencePaddingStageTest, EmptySource) {
  auto silence_padding_stage = MakeSilencePaddingStage(Fixed(10),
                                                       /*round_down_fractional_frames=*/true,
                                                       MakePacketQueueProducerStage());

  // Since source is empty, no packet should be returned.
  const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(0), 20);
  ExpectNullPacket(packet);
}

TEST(SilencePaddingStageTest, AfterOnePacket) {
  auto producer_stage = MakePacketQueueProducerStage();
  auto silence_padding_stage =
      MakeSilencePaddingStage(Fixed(5),
                              /*round_down_fractional_frames=*/true, producer_stage);

  const int16_t expected_sample = 1;
  std::vector<int16_t> source_payload(20, expected_sample);
  producer_stage->push(PacketView({kFormat, Fixed(0), 20, source_payload.data()}));

  // Source packet.
  {
    SCOPED_TRACE("Read(0, 20)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(0), 20);
    ExpectPacket(packet, Fixed(0), Fixed(20), expected_sample);
  }

  // Silence.
  {
    SCOPED_TRACE("Read(20, 10)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(20), 10);
    ExpectPacket(packet, Fixed(20), Fixed(25), 0);
  }

  // No further data.
  {
    SCOPED_TRACE("Read(25, 10)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(25), 10);
    ExpectNullPacket(packet);
  }
}

TEST(SilencePaddingStageTest, AfterTwoPackets) {
  auto producer_stage = MakePacketQueueProducerStage();
  auto silence_padding_stage =
      MakeSilencePaddingStage(Fixed(5),
                              /*round_down_fractional_frames=*/true, producer_stage);

  const int16_t expected_sample_1 = 1;
  const int16_t expected_sample_2 = 2;
  std::vector<int16_t> source_payload_1(20, expected_sample_1);
  std::vector<int16_t> source_payload_2(20, expected_sample_2);
  producer_stage->push(PacketView({kFormat, Fixed(0), 20, source_payload_1.data()}));
  producer_stage->push(PacketView({kFormat, Fixed(20), 20, source_payload_2.data()}));

  // First source packet.
  {
    SCOPED_TRACE("Read(0, 20)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(0), 20);
    ExpectPacket(packet, Fixed(0), Fixed(20), expected_sample_1);
  }

  // Second source packet.
  {
    SCOPED_TRACE("Read(20, 20)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(20), 20);
    ExpectPacket(packet, Fixed(20), Fixed(40), expected_sample_2);
  }

  // Silence.
  {
    SCOPED_TRACE("Read(40, 10)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(40), 10);
    ExpectPacket(packet, Fixed(40), Fixed(45), 0);
  }

  // No further data.
  {
    SCOPED_TRACE("Read(45, 10)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(45), 10);
    ExpectNullPacket(packet);
  }
}

TEST(SilencePaddingStageTest, SkipPacket) {
  auto producer_stage = MakePacketQueueProducerStage();
  auto silence_padding_stage =
      MakeSilencePaddingStage(Fixed(5),
                              /*round_down_fractional_frames=*/true, producer_stage);

  const int16_t expected_sample = 1;
  std::vector<int16_t> source_payload(20, expected_sample);
  producer_stage->push(PacketView({kFormat, Fixed(0), 20, source_payload.data()}));

  // If we completely skip over the source packet, this is a discontinuity. There's no need to emit
  // silence because there was no prior audio to "ring out".
  {
    SCOPED_TRACE("Read(20, 10)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(20), 10);
    ExpectNullPacket(packet);
  }
}

TEST(SilencePaddingStageTest, GapBetweenPacketsLongerThanSilence) {
  auto producer_stage = MakePacketQueueProducerStage();
  auto silence_padding_stage =
      MakeSilencePaddingStage(Fixed(5),
                              /*round_down_fractional_frames=*/true, producer_stage);

  const int16_t expected_sample = 1;
  std::vector<int16_t> source_payload(10, expected_sample);
  producer_stage->push(PacketView({kFormat, Fixed(10), 10, source_payload.data()}));
  producer_stage->push(PacketView({kFormat, Fixed(45), 10, source_payload.data()}));

  // First source packet.
  {
    SCOPED_TRACE("Read(0, 20)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(0), 20);
    ExpectPacket(packet, Fixed(10), Fixed(20), expected_sample);
  }

  // First silence.
  {
    SCOPED_TRACE("Read(20, 10)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(20), 5);
    ExpectPacket(packet, Fixed(20), Fixed(25), 0);
  }

  // Empty gap.
  {
    SCOPED_TRACE("Read(25, 20)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(25), 20);
    ExpectNullPacket(packet);
  }

  // Second source packet.
  {
    SCOPED_TRACE("Read(45, 10)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(45), 10);
    ExpectPacket(packet, Fixed(45), Fixed(55), expected_sample);
  }

  // Second silence.
  {
    SCOPED_TRACE("Read(55, 10)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(55), 10);
    ExpectPacket(packet, Fixed(55), Fixed(60), 0);
  }
}

TEST(SilencePaddingStageTest, GapBetweenPacketsShorterThanSilence) {
  auto producer_stage = MakePacketQueueProducerStage();
  auto silence_padding_stage =
      MakeSilencePaddingStage(Fixed(5),
                              /*round_down_fractional_frames=*/true, producer_stage);

  const int16_t expected_sample = 1;
  std::vector<int16_t> source_payload(10, expected_sample);
  producer_stage->push(PacketView({kFormat, Fixed(10), 10, source_payload.data()}));
  producer_stage->push(PacketView({kFormat, Fixed(21), 10, source_payload.data()}));

  // First source packet.
  {
    SCOPED_TRACE("Read(0, 20)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(0), 20);
    ExpectPacket(packet, Fixed(10), Fixed(20), expected_sample);
  }

  // First silence. Although we have configured 5 frames of silence, the second packet starts after
  // just one frame of silence, so we emit just one frame of silence.
  {
    SCOPED_TRACE("Read(20, 20)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(20), 20);
    ExpectPacket(packet, Fixed(20), Fixed(21), 0);
  }

  // Second source packet.
  {
    SCOPED_TRACE("Read(21, 20)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(21), 20);
    ExpectPacket(packet, Fixed(21), Fixed(31), expected_sample);
  }
}

TEST(SilencePaddingStageTest, GapBetweenPacketsShorterThanSilenceAndFractionalRoundDown) {
  auto producer_stage = MakePacketQueueProducerStage();
  auto silence_padding_stage =
      MakeSilencePaddingStage(Fixed(5),
                              /*round_down_fractional_frames=*/true, producer_stage);

  const int16_t expected_sample = 1;
  std::vector<int16_t> source_payload(10, expected_sample);
  producer_stage->push(PacketView({kFormat, Fixed(10), 10, source_payload.data()}));
  producer_stage->push(
      PacketView({kFormat, Fixed(21) + ffl::FromRatio(1, 2), 10, source_payload.data()}));

  // First source packet.
  {
    SCOPED_TRACE("Read(0, 20)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(0), 20);
    ExpectPacket(packet, Fixed(10), Fixed(20), expected_sample);
  }

  // First silence. Although we haveve configured 5 frames of silence, the second packet starts
  // after just 1.5 frames of silence, so we round down to 1.0 frames of silence.
  {
    SCOPED_TRACE("Read(20, 20)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(20), 20);
    ExpectPacket(packet, Fixed(20), Fixed(21), 0);
  }

  // Second source packet.
  {
    SCOPED_TRACE("Read(21, 20)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(21), 20);
    ExpectPacket(packet, Fixed(21) + ffl::FromRatio(1, 2), Fixed(31) + ffl::FromRatio(1, 2),
                 expected_sample);
  }
}

TEST(SilencePaddingStageTest, GapBetweenPacketsShorterThanSilenceAndFractionalRoundUp) {
  auto producer_stage = MakePacketQueueProducerStage();
  auto silence_padding_stage =
      MakeSilencePaddingStage(Fixed(5),
                              /*round_down_fractional_frames=*/false, producer_stage);

  const int16_t expected_sample = 1;
  std::vector<int16_t> source_payload(10, expected_sample);
  producer_stage->push(PacketView({kFormat, Fixed(10), 10, source_payload.data()}));
  producer_stage->push(
      PacketView({kFormat, Fixed(21) + ffl::FromRatio(1, 2), 10, source_payload.data()}));

  // First source packet.
  {
    SCOPED_TRACE("Read(0, 20)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(0), 20);
    ExpectPacket(packet, Fixed(10), Fixed(20), expected_sample);
  }

  // First silence. Although we have configured 5 frames of silence, the second packet starts after
  // just 1.5 frames of silence, so we round up to 2.0 frames of silence.
  {
    SCOPED_TRACE("Read(20, 20)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(20), 20);
    ExpectPacket(packet, Fixed(20), Fixed(22), 0);
  }

  // Second source packet.
  {
    SCOPED_TRACE("Read(22, 20)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(22), 20);
    ExpectPacket(packet, Fixed(21) + ffl::FromRatio(1, 2), Fixed(31) + ffl::FromRatio(1, 2),
                 expected_sample);
  }
}

TEST(SilencePaddingStageTest, GapBetweenPacketsLessThanOneFrameRoundDown) {
  auto producer_stage = MakePacketQueueProducerStage();
  auto silence_padding_stage =
      MakeSilencePaddingStage(Fixed(5),
                              /*round_down_fractional_frames=*/true, producer_stage);

  const int16_t expected_sample = 1;
  std::vector<int16_t> source_payload(10, expected_sample);
  producer_stage->push(PacketView({kFormat, Fixed(10), 10, source_payload.data()}));
  producer_stage->push(
      PacketView({kFormat, Fixed(20) + ffl::FromRatio(1, 2), 10, source_payload.data()}));

  // First source packet.
  {
    SCOPED_TRACE("Read(0, 20)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(0), 20);
    ExpectPacket(packet, Fixed(10), Fixed(20), expected_sample);
  }

  // Second source packet.
  // The gap between packets is smaller than one frame and we're rounding down,
  // so don't emit silence.
  {
    SCOPED_TRACE("Read(20, 20)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(20), 20);
    ExpectPacket(packet, Fixed(20) + ffl::FromRatio(5, 10), Fixed(30) + ffl::FromRatio(5, 10),
                 expected_sample);
  }
}

TEST(SilencePaddingStageTest, GapBetweenPacketsLessThanOneFrameRoundUp) {
  auto producer_stage = MakePacketQueueProducerStage();
  auto silence_padding_stage =
      MakeSilencePaddingStage(Fixed(5),
                              /*round_down_fractional_frames=*/false, producer_stage);

  const int16_t expected_sample = 1;
  std::vector<int16_t> source_payload(10, expected_sample);
  producer_stage->push(PacketView({kFormat, Fixed(10), 10, source_payload.data()}));
  producer_stage->push(
      PacketView({kFormat, Fixed(20) + ffl::FromRatio(1, 2), 10, source_payload.data()}));

  // First source packet.
  {
    SCOPED_TRACE("Read(0, 20)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(0), 20);
    ExpectPacket(packet, Fixed(10), Fixed(20), expected_sample);
  }

  // Second source packet.
  // The gap between packets is smaller than one frame, but we're rounding up,
  // so emit one frame of silence.
  {
    SCOPED_TRACE("Read(20, 20)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(20), 20);
    ExpectPacket(packet, Fixed(20), Fixed(21), 0);
  }

  // Now read the second source packet.
  {
    SCOPED_TRACE("Read(21, 20)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(21), 20);
    ExpectPacket(packet, Fixed(20) + ffl::FromRatio(5, 10), Fixed(30) + ffl::FromRatio(5, 10),
                 expected_sample);
  }
}

TEST(SilencePaddingStageTest, CreateRoundsUpNumberOfFrames) {
  auto producer_stage = MakePacketQueueProducerStage();
  auto silence_padding_stage =
      MakeSilencePaddingStage(ffl::FromRatio(1, 2),
                              /*round_down_fractional_frames=*/true, producer_stage);

  const int16_t expected_sample = 1;
  std::vector<int16_t> source_payload(10, expected_sample);
  producer_stage->push(PacketView({kFormat, Fixed(10), 10, source_payload.data()}));

  // Source packet.
  {
    SCOPED_TRACE("Read(0, 20)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(0), 20);
    ExpectPacket(packet, Fixed(10), Fixed(20), expected_sample);
  }

  // We asked for 0.5 frames of silence, but should get 1.0 frames.
  {
    SCOPED_TRACE("Read(20, 10)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(20), 10);
    ExpectPacket(packet, Fixed(20), Fixed(21), 0);
  }

  // No more data.
  {
    SCOPED_TRACE("Read(21, 10)");
    const auto packet = silence_padding_stage->Read(DefaultCtx(), Fixed(21), 10);
    ExpectNullPacket(packet);
  }
}

}  // namespace
}  // namespace media_audio
