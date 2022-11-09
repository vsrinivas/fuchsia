// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/mixer_source.h"

#include <lib/zx/time.h>
#include <zircon/types.h>

#include <memory>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fidl/fuchsia.mediastreams/cpp/wire_types.h"
#include "src/media/audio/lib/clock/clock.h"
#include "src/media/audio/lib/clock/clock_synchronizer.h"
#include "src/media/audio/lib/clock/synthetic_clock_realm.h"
#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/lib/processing/gain.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/packet_view.h"
#include "src/media/audio/services/mixer/mix/ptr_decls.h"
#include "src/media/audio/services/mixer/mix/simple_packet_queue_producer_stage.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"
#include "src/media/audio/services/mixer/mix/testing/fake_pipeline_thread.h"
#include "src/media/audio/services/mixer/mix/testing/test_fence.h"

namespace media_audio {
namespace {

using ::fuchsia_audio::SampleType;
using ::testing::Each;
using ::testing::UnorderedElementsAre;

constexpr uint32_t kDefaultNumChannels = 1;
constexpr uint32_t kDefaultFrameRate = 48000;
constexpr uint32_t kDefaultMaxDestFrameCountPerMix = 240;

const auto kDefaultFormat =
    Format::CreateOrDie({SampleType::kFloat32, kDefaultNumChannels, kDefaultFrameRate});
// Timeline rate of 1 fractional frame per nanosecond with respect to `kDefaultFrameRate`.
const TimelineFunction kDefaultPresentationTimeToFracFrame = TimelineFunction(
    TimelineRate(Fixed(kDefaultFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs()));

void TestAdvance(int64_t step_size = 1) {
  auto clock_realm = SyntheticClockRealm::Create();
  auto dest_clock = clock_realm->CreateClock("dest_clock", Clock::kMonotonicDomain, false);
  auto source_clock = clock_realm->CreateClock(
      "source_clock", Clock::kMonotonicDomain, false,
      /*to_clock_mono=*/TimelineFunction(TimelineRate(static_cast<float>(step_size))));

  const auto source =
      std::make_shared<SimplePacketQueueProducerStage>(SimplePacketQueueProducerStage::Args{
          .name = "source",
          .format = kDefaultFormat,
          .reference_clock = UnreadableClock(source_clock),
          .initial_thread = std::make_shared<FakePipelineThread>(1),
      });
  source->UpdatePresentationTimeToFracFrame(kDefaultPresentationTimeToFracFrame);

  // Push some packets.
  TestFence fence1;
  source->push(PacketView({kDefaultFormat, Fixed(10), 20, nullptr}), fence1.Take());
  TestFence fence2;
  source->push(PacketView({kDefaultFormat, Fixed(30), 20, nullptr}), fence2.Take());

  MixerSource mixer_source(source,
                           {.clock_sync = ClockSynchronizer::Create(
                                dest_clock, source_clock, ClockSynchronizer::Mode::WithMicroSRC),
                            .gain_ids = {GainControlId{1}, GainControlId{2}},
                            .sampler = Sampler::Create(kDefaultFormat, kDefaultFormat)},
                           {GainControlId{3}}, kDefaultMaxDestFrameCountPerMix);

  // Advance before packets, which should not close any packet fences.
  mixer_source.Advance(DefaultCtx(), kDefaultPresentationTimeToFracFrame, Fixed(0));
  EXPECT_FALSE(fence1.Done());
  EXPECT_FALSE(fence2.Done());
  EXPECT_FALSE(source->empty());

  // Advance just before the first packet, which should not close any packet fences.
  mixer_source.Advance(DefaultCtx(), kDefaultPresentationTimeToFracFrame, Fixed(10 * step_size));
  EXPECT_FALSE(fence1.Done());
  EXPECT_FALSE(fence2.Done());
  EXPECT_FALSE(source->empty());

  // Advance exactly when the first packet is fully consumed, which should close its packet fence.
  mixer_source.Advance(DefaultCtx(), kDefaultPresentationTimeToFracFrame, Fixed(30 * step_size));
  EXPECT_TRUE(fence1.Done());
  EXPECT_FALSE(fence2.Done());
  EXPECT_FALSE(source->empty());

  // Advance before the second packet is fully consumed, which should not close its packet fence.
  mixer_source.Advance(DefaultCtx(), kDefaultPresentationTimeToFracFrame, Fixed(40 * step_size));
  EXPECT_TRUE(fence1.Done());
  EXPECT_FALSE(fence2.Done());
  EXPECT_FALSE(source->empty());

  // Advance beyond when the second packet is fully consumed, which should close its packet fence.
  mixer_source.Advance(DefaultCtx(), kDefaultPresentationTimeToFracFrame, Fixed(100 * step_size));
  EXPECT_TRUE(fence1.Done());
  EXPECT_TRUE(fence2.Done());
  EXPECT_TRUE(source->empty());
}

void TestMix(int64_t step_size = 1) {
  MixerGainControls mixer_gain_controls;

  auto clock_realm = SyntheticClockRealm::Create();
  auto dest_clock = clock_realm->CreateClock("dest_clock", Clock::kMonotonicDomain, false);
  auto source_clock = clock_realm->CreateClock(
      "source_clock", Clock::kMonotonicDomain, false,
      /*to_clock_mono=*/TimelineFunction(TimelineRate(static_cast<float>(step_size))));

  ClockSnapshots clock_snapshots;
  clock_snapshots.AddClock(dest_clock);
  clock_snapshots.AddClock(source_clock);
  clock_snapshots.Update(zx::time(0));
  mixer_gain_controls.Add(GainControlId{1}, GainControl(UnreadableClock(source_clock)));
  mixer_gain_controls.Add(GainControlId{2}, GainControl(UnreadableClock(source_clock)));
  mixer_gain_controls.Add(GainControlId{3}, GainControl(UnreadableClock(dest_clock)));
  mixer_gain_controls.Advance(clock_snapshots, zx::time(0));

  const auto source =
      std::make_shared<SimplePacketQueueProducerStage>(SimplePacketQueueProducerStage::Args{
          .name = "source",
          .format = kDefaultFormat,
          .reference_clock = UnreadableClock(source_clock),
          .initial_thread = std::make_shared<FakePipelineThread>(1),
      });
  source->UpdatePresentationTimeToFracFrame(kDefaultPresentationTimeToFracFrame);

  const int64_t dest_frame_count = 10;

  // Push a single packet with a constant value.
  std::vector<float> payload(2 * dest_frame_count * step_size, 4.0f);
  source->push(
      PacketView({kDefaultFormat, Fixed(0), static_cast<int64_t>(payload.size()), payload.data()}));

  MixJobContext ctx(clock_snapshots, zx::time(0), zx::time(10));
  MixerSource mixer_source(source,
                           {.clock_sync = ClockSynchronizer::Create(
                                dest_clock, source_clock, ClockSynchronizer::Mode::WithMicroSRC),
                            .gain_ids = {GainControlId{1}, GainControlId{2}},
                            .sampler = Sampler::Create(kDefaultFormat, kDefaultFormat)},
                           {GainControlId{3}}, dest_frame_count);

  // Set the first source gain to a constant value.
  mixer_gain_controls.Get(GainControlId{1}).SetGain(ScaleToDb(2.0f));
  mixer_gain_controls.Advance(clock_snapshots, zx::time(0));

  // Fill the gain buffer which should set the gain state to the constant value.
  mixer_source.PrepareSourceGainForNextMix(
      ctx, mixer_gain_controls, kDefaultPresentationTimeToFracFrame, 0, dest_frame_count);
  EXPECT_FLOAT_EQ(mixer_source.gain().scale, 2.0f);
  EXPECT_EQ(mixer_source.gain().type, GainType::kNonUnity);

  // Mix source onto destination, which should fill `dest_samples` with the constant value.
  std::vector<float> dest_samples(dest_frame_count, 0.0f);
  EXPECT_TRUE(mixer_source.Mix(ctx, kDefaultPresentationTimeToFracFrame, Fixed(0), dest_frame_count,
                               dest_samples.data(), /*accumulate=*/false));
  EXPECT_THAT(dest_samples, Each(2.0f * 4.0f));

  // Set the destination gain to be muted.
  mixer_gain_controls.Get(GainControlId{3}).SetMute(true);
  mixer_gain_controls.Advance(clock_snapshots, zx::time(0));

  // Fill the gain buffer which should set the gain state to silent.
  mixer_source.PrepareSourceGainForNextMix(
      ctx, mixer_gain_controls, kDefaultPresentationTimeToFracFrame, 0, dest_frame_count);
  EXPECT_EQ(mixer_source.gain().type, GainType::kSilent);

  // Mix source onto destination again, which should fill `dest_samples` with silence.
  EXPECT_FALSE(mixer_source.Mix(ctx, kDefaultPresentationTimeToFracFrame, Fixed(dest_frame_count),
                                dest_frame_count, dest_samples.data(), /*accumulate=*/false));
  EXPECT_THAT(dest_samples, Each(0.0f));
}

TEST(MixerSourceTest, Advance) { TestAdvance(); }

TEST(MixerSourceTest, AdvanceWithStepSize) { TestAdvance(/*step_size=*/4); }

TEST(MixerSourceTest, Mix) { TestMix(); }

TEST(MixerSourceTest, MixWithStepSize) { TestMix(/*step_size=*/2); }

TEST(MixerSourceTest, PrepareSourceGainForNextMix) {
  MixerGainControls mixer_gain_controls;
  mixer_gain_controls.Add(GainControlId{1}, GainControl(DefaultUnreadableClock()));
  mixer_gain_controls.Add(GainControlId{2}, GainControl(DefaultUnreadableClock()));
  mixer_gain_controls.Add(GainControlId{3}, GainControl(DefaultUnreadableClock()));
  mixer_gain_controls.Advance(DefaultClockSnapshots(), zx::time(0));

  const auto source = MakeDefaultPacketQueue(kDefaultFormat);
  const auto presentation_time_to_frac_frame =
      TimelineFunction(TimelineRate(kOneFrame.raw_value(), 1));

  MixerSource mixer_source(source,
                           {.clock_sync = DefaultClockSync(),
                            .gain_ids = {GainControlId{1}, GainControlId{2}},
                            .sampler = Sampler::Create(kDefaultFormat, kDefaultFormat)},
                           {GainControlId{3}}, 100);

  // All gain controls should initially have unity gains.
  mixer_source.PrepareSourceGainForNextMix(DefaultCtx(), mixer_gain_controls,
                                           presentation_time_to_frac_frame, 0, 50);
  EXPECT_EQ(mixer_source.gain().type, GainType::kUnity);

  // Set first source gain to be muted.
  mixer_gain_controls.Get(GainControlId{1}).SetMute(true);
  mixer_gain_controls.Advance(DefaultClockSnapshots(), zx::time(1));

  // Fill the gain buffer from the middle, which should fill in the scale ramp.
  mixer_source.PrepareSourceGainForNextMix(DefaultCtx(), mixer_gain_controls,
                                           presentation_time_to_frac_frame, 50, 100);
  EXPECT_EQ(mixer_source.gain().type, GainType::kRamping);
  for (int64_t i = 0; i < 100; ++i) {
    EXPECT_FLOAT_EQ(mixer_source.gain().scale_ramp[i], (i < 50) ? kUnityGainScale : 0.0f) << i;
  }

  // Fill the gain buffer again from the beginning, which should reset it back to silent gain.
  mixer_source.PrepareSourceGainForNextMix(DefaultCtx(), mixer_gain_controls,
                                           presentation_time_to_frac_frame, 0, 50);
  EXPECT_EQ(mixer_source.gain().type, GainType::kSilent);

  // Unmute the first gain, and set the second gain to a constant gain value.
  mixer_gain_controls.Get(GainControlId{1}).SetMute(false);
  mixer_gain_controls.Get(GainControlId{2}).SetGain(ScaleToDb(10.0f));
  mixer_gain_controls.Advance(DefaultClockSnapshots(), zx::time(2));

  // Fill the source gain from the middle, which should again fill in the scale ramp.
  mixer_source.PrepareSourceGainForNextMix(DefaultCtx(), mixer_gain_controls,
                                           presentation_time_to_frac_frame, 50, 100);
  EXPECT_EQ(mixer_source.gain().type, GainType::kRamping);
  for (int64_t i = 0; i < 100; ++i) {
    EXPECT_FLOAT_EQ(mixer_source.gain().scale_ramp[i], (i < 50) ? 0.0f : 10.0f) << i;
  }

  // Set the third gain to be ramping with 4.0f scale change per frame.
  mixer_gain_controls.Get(GainControlId{3}).SetGain(ScaleToDb(5.0f), GainRamp{zx::nsec(1)});
  mixer_gain_controls.Advance(DefaultClockSnapshots(), zx::time(2));

  // Fill the first 10 frames of the source gain, which should combine the constant scale of the
  // second gain with the ramp of the third gain.
  mixer_source.PrepareSourceGainForNextMix(DefaultCtx(), mixer_gain_controls,
                                           presentation_time_to_frac_frame, 0, 10);
  EXPECT_EQ(mixer_source.gain().type, GainType::kRamping);
  for (int64_t i = 0; i < 10; ++i) {
    EXPECT_FLOAT_EQ(mixer_source.gain().scale_ramp[i], static_cast<float>(10 + 40 * i)) << i;
  }

  // Reset the gain ramp to another constant value, and fill the rest of the gain buffer.
  mixer_gain_controls.Get(GainControlId{3}).SetGain(ScaleToDb(2.0f));
  mixer_gain_controls.Advance(DefaultClockSnapshots(), zx::time(3));

  mixer_source.PrepareSourceGainForNextMix(DefaultCtx(), mixer_gain_controls,
                                           presentation_time_to_frac_frame, 10, 100);
  EXPECT_EQ(mixer_source.gain().type, GainType::kRamping);
  for (int64_t i = 0; i < 100; ++i) {
    EXPECT_FLOAT_EQ(mixer_source.gain().scale_ramp[i],
                    (i < 10) ? static_cast<float>(10 + 40 * i) : 20.0f)
        << i;
  }

  // Finally set the gain to a value less that `kMinGainDb`, which should reset it back to silent.
  mixer_gain_controls.Get(GainControlId{3}).SetGain(kMinGainDb - 12.0f);
  mixer_gain_controls.Advance(DefaultClockSnapshots(), zx::time(4));

  mixer_source.PrepareSourceGainForNextMix(DefaultCtx(), mixer_gain_controls,
                                           presentation_time_to_frac_frame, 0, 100);
  EXPECT_EQ(mixer_source.gain().type, GainType::kSilent);
}

TEST(MixerSourceTest, SetDestGains) {
  const auto source = MakeDefaultPacketQueue(kDefaultFormat);

  MixerSource mixer_source(source,
                           {.clock_sync = DefaultClockSync(),
                            .gain_ids = {GainControlId{1}, GainControlId{2}},
                            .sampler = Sampler::Create(kDefaultFormat, kDefaultFormat)},
                           {GainControlId{3}}, kDefaultMaxDestFrameCountPerMix);
  EXPECT_THAT(mixer_source.all_gain_ids(),
              UnorderedElementsAre(GainControlId{1}, GainControlId{2}, GainControlId{3}));

  mixer_source.SetDestGains({GainControlId{2}, GainControlId{4}, GainControlId{5}});
  EXPECT_THAT(
      mixer_source.all_gain_ids(),
      UnorderedElementsAre(GainControlId{1}, GainControlId{2}, GainControlId{4}, GainControlId{5}));
}

}  // namespace
}  // namespace media_audio
