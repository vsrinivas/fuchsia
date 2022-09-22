// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/mixer_stage.h"

#include <memory>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fidl/fuchsia.mediastreams/cpp/wire_types.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/lib/processing/gain.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/mix/packet_view.h"
#include "src/media/audio/services/mixer/mix/simple_packet_queue_producer_stage.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"
#include "src/media/audio/services/mixer/mix/testing/test_fence.h"

namespace media_audio {
namespace {

using ::fuchsia_mediastreams::wire::AudioSampleFormat;
using ::testing::FloatEq;
using ::testing::Pointee;

// One frame per nanosecond.
const auto kDefaultFormat = Format::CreateOrDie({AudioSampleFormat::kFloat, 1, zx::sec(1).get()});
const auto kDefaultPresentationTimeToFracFrame =
    TimelineFunction(TimelineRate(kOneFrame.raw_value(), 1));

TEST(MixerStageTest, Advance) {
  MixerStage mixer_stage("mixer", kDefaultFormat, DefaultClock(), 10);
  mixer_stage.UpdatePresentationTimeToFracFrame(kDefaultPresentationTimeToFracFrame);

  // Add some sources.
  const auto source_1 = MakeDefaultPacketQueue(kDefaultFormat, "source-1");
  source_1->UpdatePresentationTimeToFracFrame(kDefaultPresentationTimeToFracFrame);
  mixer_stage.AddSource(source_1, {.clock_sync = DefaultClockSync(),
                                   .sampler = Sampler::Create(kDefaultFormat, kDefaultFormat)});
  const auto source_2 = MakeDefaultPacketQueue(kDefaultFormat, "source-2");
  source_2->UpdatePresentationTimeToFracFrame(kDefaultPresentationTimeToFracFrame);
  mixer_stage.AddSource(source_2, {.clock_sync = DefaultClockSync(),
                                   .sampler = Sampler::Create(kDefaultFormat, kDefaultFormat)});

  // Push some packets.
  TestFence fence1;
  source_1->push(PacketView({kDefaultFormat, Fixed(10), 20, nullptr}), fence1.Take());
  TestFence fence2;
  source_2->push(PacketView({kDefaultFormat, Fixed(30), 20, nullptr}), fence2.Take());
  TestFence fence3;
  source_1->push(PacketView({kDefaultFormat, Fixed(40), 20, nullptr}), fence3.Take());

  // Advance before packets, which should not close any packet fences.
  mixer_stage.Advance(DefaultCtx(), Fixed(0));
  EXPECT_FALSE(fence1.Done());
  EXPECT_FALSE(fence2.Done());
  EXPECT_FALSE(source_1->empty());
  EXPECT_FALSE(source_2->empty());

  // Advance just before the first packet, which should not close any packet fences.
  mixer_stage.Advance(DefaultCtx(), Fixed(10));
  EXPECT_FALSE(fence1.Done());
  EXPECT_FALSE(fence2.Done());
  EXPECT_FALSE(fence3.Done());
  EXPECT_FALSE(source_1->empty());
  EXPECT_FALSE(source_2->empty());

  // Advance exactly when the first packet is fully consumed, which should close its packet fence.
  mixer_stage.Advance(DefaultCtx(), Fixed(30));
  EXPECT_TRUE(fence1.Done());
  EXPECT_FALSE(fence2.Done());
  EXPECT_FALSE(fence3.Done());
  EXPECT_FALSE(source_1->empty());
  EXPECT_FALSE(source_2->empty());

  // Advance before the second packet is fully consumed, which should not close its packet fence.
  mixer_stage.Advance(DefaultCtx(), Fixed(40));
  EXPECT_TRUE(fence1.Done());
  EXPECT_FALSE(fence2.Done());
  EXPECT_FALSE(fence3.Done());
  EXPECT_FALSE(source_1->empty());
  EXPECT_FALSE(source_2->empty());

  // Advance beyond when all packets are fully consumed, which should close their packet fence.
  mixer_stage.Advance(DefaultCtx(), Fixed(100));
  EXPECT_TRUE(fence1.Done());
  EXPECT_TRUE(fence2.Done());
  EXPECT_TRUE(fence3.Done());
  EXPECT_TRUE(source_1->empty());
  EXPECT_TRUE(source_2->empty());
}

TEST(MixerStageTest, Read) {
  MixerStage mixer_stage("mixer", kDefaultFormat, DefaultClock(), 5);
  mixer_stage.UpdatePresentationTimeToFracFrame(kDefaultPresentationTimeToFracFrame);

  // Add two destination gain controls with constant gains, resulting in a scale of `10.0f`.
  auto& gain_controls = mixer_stage.gain_controls();
  gain_controls.Add(GainControlId{10}, GainControl(DefaultClock()));
  gain_controls.Get(GainControlId{10}).SetGain(ScaleToDb(2.0f));
  gain_controls.Add(GainControlId{20}, GainControl(DefaultClock()));
  gain_controls.Get(GainControlId{20}).SetGain(ScaleToDb(5.0f));
  mixer_stage.SetDestGains({GainControlId{10}, GainControlId{20}});

  // Add some sources with constant gains, and push packets to each source.
  const int64_t source_count = 4;
  std::vector<float> source_payload(10, 1.0f);
  for (int64_t i = 1; i <= source_count; ++i) {
    // Each source data is scaled by its index.
    const auto gain_id = GainControlId{static_cast<uint64_t>(i)};
    gain_controls.Add(gain_id, GainControl(DefaultClock()));
    gain_controls.Get(gain_id).SetGain(ScaleToDb(static_cast<float>(i)));

    const auto source = MakeDefaultPacketQueue(kDefaultFormat, "source" + std::to_string(i));
    source->UpdatePresentationTimeToFracFrame(kDefaultPresentationTimeToFracFrame);
    mixer_stage.AddSource(source, {.clock_sync = DefaultClockSync(),
                                   .gain_ids = {gain_id},
                                   .sampler = Sampler::Create(kDefaultFormat, kDefaultFormat)});

    // Each source contributes to the mix starting with its index as offset.
    source->push(PacketView({kDefaultFormat, Fixed(10 + i), 10, source_payload.data()}));
  }

  {
    // Read the first 5 frames, which should contain each source start contributing in each index.
    SCOPED_TRACE("Read(10, 5)");
    const std::vector<float> expected = {0.0f, 10.0f, 30.0f, 60.0f, 100.0f};
    const auto packet = mixer_stage.Read(DefaultCtx(), Fixed(10), 5);
    ASSERT_TRUE(packet.has_value());
    EXPECT_EQ(packet->start(), Fixed(10));
    EXPECT_EQ(packet->length(), 5);
    for (int64_t i = 0; i < packet->length(); ++i) {
      EXPECT_FLOAT_EQ(static_cast<float*>(packet->payload())[i], expected[i]) << i;
    }
  }

  {
    // Read the next 5 frames, which should contain the contributions from all sources now:
    //   `10.0f * 1.0f + 10.0f * 2.0f + 10.0f * 3.0f + 10.0f * 4.0f = 100.0f`
    SCOPED_TRACE("Read(15, 5)");
    const auto packet = mixer_stage.Read(DefaultCtx(), Fixed(15), 5);
    ASSERT_TRUE(packet.has_value());
    EXPECT_EQ(packet->start(), Fixed(15));
    EXPECT_EQ(packet->length(), 5);
    for (int64_t i = 0; i < packet->length(); ++i) {
      EXPECT_FLOAT_EQ(static_cast<float*>(packet->payload())[i], 100.0f) << i;
    }
  }

  {
    // Read the next 5 frames, which should contain each source stop contributing in each index.
    SCOPED_TRACE("Read(20, 5)");
    const std::vector<float> expected = {100.0f, 90.0f, 70.0f, 40.0f, 0.0f};
    const auto packet = mixer_stage.Read(DefaultCtx(), Fixed(20), 5);
    ASSERT_TRUE(packet.has_value());
    EXPECT_EQ(packet->start(), Fixed(20));
    EXPECT_EQ(packet->length(), 5);
    for (int64_t i = 0; i < packet->length(); ++i) {
      EXPECT_FLOAT_EQ(static_cast<float*>(packet->payload())[i], expected[i]) << i;
    }
  }

  {
    // Attempt to read the next 5 frames, which should only contain the padded frame of silence.
    SCOPED_TRACE("Read(25, 5)");
    const auto packet = mixer_stage.Read(DefaultCtx(), Fixed(25), 5);
    ASSERT_TRUE(packet.has_value());
    EXPECT_EQ(packet->start(), Fixed(25));
    EXPECT_EQ(packet->length(), 5);
    for (int64_t i = 0; i < packet->length(); ++i) {
      EXPECT_FLOAT_EQ(static_cast<float*>(packet->payload())[i], 0.0f) << i;
    }
  }

  // Attempt to read more, which should not return any more data.
  SCOPED_TRACE("Read(30, 5)");
  EXPECT_FALSE(mixer_stage.Read(DefaultCtx(), Fixed(30), 5));
}

TEST(MixerStageTest, ReadMoreThanMaxFrameCount) {
  // Set maximum frame count to 5.
  MixerStage mixer_stage("mixer", kDefaultFormat, DefaultClock(), 5);
  mixer_stage.UpdatePresentationTimeToFracFrame(kDefaultPresentationTimeToFracFrame);

  // Add two sources, and push packets to each source with a gap in-between.
  const auto source_1 = MakeDefaultPacketQueue(kDefaultFormat, "source-1");
  source_1->UpdatePresentationTimeToFracFrame(kDefaultPresentationTimeToFracFrame);
  mixer_stage.AddSource(source_1, {.clock_sync = DefaultClockSync(),
                                   .sampler = Sampler::Create(kDefaultFormat, kDefaultFormat)});
  const auto source_2 = MakeDefaultPacketQueue(kDefaultFormat, "source-2");
  source_2->UpdatePresentationTimeToFracFrame(kDefaultPresentationTimeToFracFrame);
  mixer_stage.AddSource(source_2, {.clock_sync = DefaultClockSync(),
                                   .sampler = Sampler::Create(kDefaultFormat, kDefaultFormat)});

  std::vector<float> source_1_payload(10, 1.0f);
  source_1->push(PacketView({kDefaultFormat, Fixed(12), 10, source_1_payload.data()}));

  std::vector<float> source_2_payload(10, 2.0f);
  source_2->push(PacketView({kDefaultFormat, Fixed(15), 10, source_2_payload.data()}));

  // Read the first 10 frames, which should not contain any data.
  SCOPED_TRACE("Read(0, 10)");
  EXPECT_FALSE(mixer_stage.Read(DefaultCtx(), Fixed(0), 10).has_value());

  {
    // Read another 10 frames, which should only contain the first source data starting at 12.
    SCOPED_TRACE("Read(10, 10)");
    const auto packet = mixer_stage.Read(DefaultCtx(), Fixed(10), 10);
    ASSERT_TRUE(packet.has_value());
    EXPECT_EQ(packet->start(), Fixed(10));
    EXPECT_EQ(packet->length(), 5);
    for (int64_t i = 0; i < packet->length(); ++i) {
      EXPECT_FLOAT_EQ(static_cast<float*>(packet->payload())[i], (i >= 2) ? 1.0f : 0.0f) << i;
    }
  }

  {
    // Attempt to read again from where we left off for 5 frames, which should contain both sources.
    SCOPED_TRACE("Read(15, 5)");
    const auto packet = mixer_stage.Read(DefaultCtx(), Fixed(15), 5);
    ASSERT_TRUE(packet.has_value());
    EXPECT_EQ(packet->start(), Fixed(15));
    EXPECT_EQ(packet->length(), 5);
    for (int64_t i = 0; i < packet->length(); ++i) {
      EXPECT_FLOAT_EQ(static_cast<float*>(packet->payload())[i], 3.0f) << i;
    }
  }

  {
    // Read another 10 frames, which should contain both sources until first source ends at 22, and
    // only the second source for the remainder of 3 frames.
    SCOPED_TRACE("Read(20, 10)");
    const auto packet = mixer_stage.Read(DefaultCtx(), Fixed(20), 10);
    ASSERT_TRUE(packet.has_value());
    EXPECT_EQ(packet->start(), Fixed(20));
    EXPECT_EQ(packet->length(), 5);
    for (int64_t i = 0; i < packet->length(); ++i) {
      EXPECT_FLOAT_EQ(static_cast<float*>(packet->payload())[i], (i >= 2) ? 2.0f : 3.0f) << i;
    }
  }

  {
    // Attempt to read another 10 frames from where we left off, which should only contain 5 frames
    // of silence due to the leftover padded frame of silence at 25.
    SCOPED_TRACE("Read(25, 10)");
    const auto packet = mixer_stage.Read(DefaultCtx(), Fixed(25), 10);
    ASSERT_TRUE(packet.has_value());
    EXPECT_EQ(packet->start(), Fixed(25));
    EXPECT_EQ(packet->length(), 5);
    EXPECT_THAT(static_cast<float*>(packet->payload()), Pointee(FloatEq(0.0f)));
  }

  // Attempt to read more, which should not return any more data.
  SCOPED_TRACE("Read(30, 10)");
  EXPECT_FALSE(mixer_stage.Read(DefaultCtx(), Fixed(30), 10));
}

TEST(MixerStageTest, ReadSilent) {
  MixerStage mixer_stage("mixer", kDefaultFormat, DefaultClock(), 10);
  mixer_stage.UpdatePresentationTimeToFracFrame(kDefaultPresentationTimeToFracFrame);

  // Mute destination gain.
  const auto dest_gain_id = GainControlId{1};
  auto& gain_controls = mixer_stage.gain_controls();
  gain_controls.Add(dest_gain_id, GainControl(DefaultClock()));
  gain_controls.Get(dest_gain_id).SetMute(true);
  mixer_stage.SetDestGains({dest_gain_id});

  // Add some sources without gain controls, and push packets to each source.
  const size_t source_count = 4;
  std::vector<float> source_payload(10, 1.0f);
  for (size_t i = 1; i <= source_count; ++i) {
    const auto source = MakeDefaultPacketQueue(kDefaultFormat, "source" + std::to_string(i));
    source->UpdatePresentationTimeToFracFrame(kDefaultPresentationTimeToFracFrame);
    mixer_stage.AddSource(source, {.clock_sync = DefaultClockSync(),
                                   .sampler = Sampler::Create(kDefaultFormat, kDefaultFormat)});
    source->push(PacketView({kDefaultFormat, Fixed(10), 10, source_payload.data()}));
  }

  // Read first 5 frames, which should return `nullopt` since the destination gain is muted.
  EXPECT_FALSE(mixer_stage.Read(DefaultCtx(), Fixed(10), 5).has_value());

  // Unmute destination gain.
  gain_controls.Get(dest_gain_id).SetMute(false);

  // Read the next 5 frames, which should contain the mixed source data now.
  const auto packet = mixer_stage.Read(DefaultCtx(), Fixed(15), 5);
  ASSERT_TRUE(packet.has_value());
  EXPECT_EQ(packet->start(), Fixed(15));
  EXPECT_EQ(packet->length(), 5);
  for (int64_t i = 0; i < packet->length(); ++i) {
    EXPECT_FLOAT_EQ(static_cast<float*>(packet->payload())[i], static_cast<float>(source_count));
  }
}

TEST(MixerStageTest, ReadNoInput) {
  MixerStage mixer_stage("mixer", kDefaultFormat, DefaultClock(), 10);
  mixer_stage.UpdatePresentationTimeToFracFrame(kDefaultPresentationTimeToFracFrame);
  EXPECT_FALSE(mixer_stage.Read(DefaultCtx(), Fixed(0), 10).has_value());
}

TEST(MixerStageTest, SetDestGains) {
  MixerStage mixer_stage("mixer", kDefaultFormat, DefaultClock(), 1);
  mixer_stage.UpdatePresentationTimeToFracFrame(kDefaultPresentationTimeToFracFrame);
  auto& gain_controls = mixer_stage.gain_controls();

  // Add the first source with a source gain control {1} that is set to a constant gain of `10.0f`.
  const auto gain_id_1 = GainControlId{1};
  gain_controls.Add(gain_id_1, GainControl(DefaultClock()));
  gain_controls.Get(gain_id_1).SetGain(ScaleToDb(10.0f));

  const auto source_1 = MakeDefaultPacketQueue(kDefaultFormat, "source-1");
  source_1->UpdatePresentationTimeToFracFrame(kDefaultPresentationTimeToFracFrame);
  mixer_stage.AddSource(source_1, {.clock_sync = DefaultClockSync(),
                                   .gain_ids = {gain_id_1},
                                   .sampler = Sampler::Create(kDefaultFormat, kDefaultFormat)});

  float value_1 = 1.0f;
  {
    // Push and read a source packet, which should be scaled by the source gain control {1}.
    SCOPED_TRACE("Read(10, 1)");
    source_1->push(PacketView({kDefaultFormat, Fixed(10), 1, &value_1}));
    const auto packet = mixer_stage.Read(DefaultCtx(), Fixed(10), 1);
    ASSERT_TRUE(packet.has_value());
    EXPECT_EQ(packet->start(), Fixed(10));
    EXPECT_EQ(packet->length(), 1);
    EXPECT_THAT(static_cast<float*>(packet->payload()), Pointee(FloatEq(10.0f)));
  }

  // Add a destination gain control {2} and set it to another constant gain of `4.0f`.
  const auto gain_id_2 = GainControlId{2};
  gain_controls.Add(gain_id_2, GainControl(DefaultClock()));
  gain_controls.Get(gain_id_2).SetGain(ScaleToDb(4.0f));
  mixer_stage.SetDestGains({gain_id_2});

  {
    // Push and read another source packet, which should be scaled this time by both the source and
    // destination gain controls {1, 2}, resulting in: `1.0f * 10.0f * 4.0f = 40.0f`.
    SCOPED_TRACE("Read(11, 1)");
    source_1->push(PacketView({kDefaultFormat, Fixed(11), 1, &value_1}));
    const auto packet = mixer_stage.Read(DefaultCtx(), Fixed(11), 1);
    ASSERT_TRUE(packet.has_value());
    EXPECT_EQ(packet->start(), Fixed(11));
    EXPECT_EQ(packet->length(), 1);
    EXPECT_THAT(static_cast<float*>(packet->payload()), Pointee(FloatEq(40.0f)));
  }

  // Add the second source with no gain controls.
  const auto source_2 = MakeDefaultPacketQueue(kDefaultFormat, "source-2");
  source_2->UpdatePresentationTimeToFracFrame(kDefaultPresentationTimeToFracFrame);
  mixer_stage.AddSource(source_2, {.clock_sync = DefaultClockSync(),
                                   .sampler = Sampler::Create(kDefaultFormat, kDefaultFormat)});

  float value_2 = 2.0f;
  {
    // Push packets for each source and read, where the first source should be scaled again by the
    // gain controls {1, 2}, and the second source should only be scaled by the destination gain
    // control {2}, resulting in: `1.0f * 10.0f * 4.0f + 2.0f * 4.0f = 48.0f`.
    SCOPED_TRACE("Read(12, 1)");
    source_1->push(PacketView({kDefaultFormat, Fixed(12), 1, &value_1}));
    source_2->push(PacketView({kDefaultFormat, Fixed(12), 1, &value_2}));
    const auto packet = mixer_stage.Read(DefaultCtx(), Fixed(12), 1);
    ASSERT_TRUE(packet.has_value());
    EXPECT_EQ(packet->start(), Fixed(12));
    EXPECT_EQ(packet->length(), 1);
    EXPECT_THAT(static_cast<float*>(packet->payload()), Pointee(FloatEq(48.0f)));
  }

  // Update the destination gain to another constant value of `3.0f`.
  gain_controls.Get(gain_id_2).SetGain(ScaleToDb(3.0f));

  {
    // Push packets for each source one more time, where sources should be scaled the same way, but
    // with the updated destination gain, resulting in: `1.0f * 10.0f * 3.0f + 2.0f * 3.0f = 36.0f`.
    SCOPED_TRACE("Read(13, 1)");
    source_1->push(PacketView({kDefaultFormat, Fixed(13), 1, &value_1}));
    source_2->push(PacketView({kDefaultFormat, Fixed(13), 1, &value_2}));
    const auto packet = mixer_stage.Read(DefaultCtx(), Fixed(13), 1);
    ASSERT_TRUE(packet.has_value());
    EXPECT_EQ(packet->start(), Fixed(13));
    EXPECT_EQ(packet->length(), 1);
    EXPECT_THAT(static_cast<float*>(packet->payload()), Pointee(FloatEq(36.0f)));
  }

  // Mute the first source gain control.
  gain_controls.Get(gain_id_1).SetMute(true);

  {
    // Push packets for each source one final time, which should only contain the second source,
    // resulting in: `2.0f * 3.0f = 6.0f`.
    SCOPED_TRACE("Read(14, 1)");
    source_1->push(PacketView({kDefaultFormat, Fixed(14), 1, &value_1}));
    source_2->push(PacketView({kDefaultFormat, Fixed(14), 1, &value_2}));
    const auto packet = mixer_stage.Read(DefaultCtx(), Fixed(14), 1);
    ASSERT_TRUE(packet.has_value());
    EXPECT_EQ(packet->start(), Fixed(14));
    EXPECT_EQ(packet->length(), 1);
    EXPECT_THAT(static_cast<float*>(packet->payload()), Pointee(FloatEq(6.0f)));
  }
}

}  // namespace
}  // namespace media_audio
