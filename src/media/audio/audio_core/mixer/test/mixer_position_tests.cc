// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iterator>

#include <fbl/algorithm.h>

#include "src/media/audio/audio_core/mixer/no_op.h"
#include "src/media/audio/audio_core/mixer/test/mixer_tests_shared.h"

using testing::FloatEq;
using testing::Pointwise;

namespace media::audio::test {

// Convenience abbreviation within this source file to shorten names
using Resampler = ::media::audio::Mixer::Resampler;

//
// Timing (Resampling) tests
//
// Sync/timing correctness, to the sample level
// Verify correct FROM and TO locations, and quantity.
//
// When doing direct bit-for-bit comparisons in these tests, we must factor in
// the left-shift biasing that is done while converting input data into the
// internal format of our accumulator.  For this reason, all "expect" values are
// specified at a higher-than-needed precision of 24-bit, and then normalized
// down to the actual pipeline width.
//
// Each test contains cases that exercise different code paths within the
// samplers.  A mix job's length is limited by the quantities of source data and
// output needed -- whichever is smaller. For this reason, we explicitly note
// places where we check "supply > demand", vs. "demand > supply", vs. "supply
// == demand". We used the PointSampler in earlier tests, so we already know
// "Supply == Demand" works there. When setting up each case, the so-called
// "supply" is determined by source_frames, and source_offset (into those frames).
// Likewise "demand" is determined by dest_frames and dest_offset into
// dest_frames.

// Verify that the samplers mix to/from correct buffer locations. Also ensure
// that they don't touch other buffer sections, regardless of 'accumulate'.
// This first test uses integer lengths/offsets, and a step_size of ONE.
void TestBasicPosition(Resampler samplerType) {
  bool mix_result;

  auto mixer =
      SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000, samplerType);
  ASSERT_NE(mixer, nullptr);

  //
  // Check: source supply equals destination demand.
  // Source (offset 2 of 5) has 3. Destination (offset 1 of 4) wants 3.
  int16_t source[] = {1, 0xC, 0x7B, 0x4D2, 0x3039};
  auto source_frames = 5;
  auto source_offset = Fixed(2);
  int64_t dest_frames = 4, dest_offset = 1;

  // Mix will add source[2,3,4] to accum[1,2,3]
  float accum[] = {-0x00002000, -0x00017000, -0x000EA000, -0x00929000, -0x05BA0000};
  float expect[] = {-0x00002000, 0x00064000, 0x003E8000, 0x02710000, -0x05BA0000};
  NormalizeInt28ToPipelineBitwidth(accum, std::size(accum));
  NormalizeInt28ToPipelineBitwidth(expect, std::size(expect));

  mix_result =
      mixer->Mix(accum, dest_frames, &dest_offset, source, source_frames, &source_offset, true);

  EXPECT_TRUE(mix_result);
  EXPECT_EQ(4, dest_offset);
  EXPECT_EQ(Fixed(5), source_offset) << std::hex << source_offset.raw_value();
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));

  //
  // Check: source supply exceeds destination demand.
  // Source (offset 0 of 4) has 4. Destination (offset 2 of 4) wants 2.
  source_frames = 4;
  source_offset = Fixed(0);
  dest_frames = 4;
  dest_offset = 2;
  // Mix will add source[0,1] to accum2[2,3]
  float accum2[] = {-0x00002000, -0x00017000, -0x000EA000, -0x00929000, -0x05BA0000};
  float expect2[] = {-0x00002000, -0x00017000, -0x000E9000, -0x0091D000, -0x05BA0000};
  NormalizeInt28ToPipelineBitwidth(accum2, std::size(accum2));
  NormalizeInt28ToPipelineBitwidth(expect2, std::size(expect2));

  mix_result =
      mixer->Mix(accum2, dest_frames, &dest_offset, source, source_frames, &source_offset, true);

  EXPECT_FALSE(mix_result);
  EXPECT_EQ(4, dest_offset);
  EXPECT_EQ(Fixed(2), source_offset) << std::hex << source_offset.raw_value();
  EXPECT_THAT(accum2, Pointwise(FloatEq(), expect2));

  //
  // Check: destination demand exceeds source supply.
  // Source (offset 2 of 3) has 1. Destination (offset 0 of 4) wants 4.
  source_offset = Fixed(2);
  dest_offset = 0;
  // Mix will move source[2] to accum[0]
  float expect3[] = {0x0007B000, -0x00017000, -0x000E9000, -0x0091D000, -0x05BA0000};
  NormalizeInt28ToPipelineBitwidth(expect3, std::size(expect3));

  mix_result = mixer->Mix(accum2, 4, &dest_offset, source, 3, &source_offset, false);

  EXPECT_TRUE(mix_result);
  EXPECT_EQ(1, dest_offset);
  EXPECT_EQ(Fixed(3), source_offset) << std::hex << source_offset.raw_value();
  EXPECT_THAT(accum2, Pointwise(FloatEq(), expect3));
}

// Validate basic (frame-level) position for SampleAndHold resampler.
TEST(Position, Position_Basic_Point) { TestBasicPosition(Resampler::SampleAndHold); }

// For PointSampler, test sample placement when given fractional position.
// Ensure it doesn't touch other buffer sections, regardless of 'accumulate'
// flag. Check when supply > demand and vice versa (we already know = works).
// This test uses fractional lengths/offsets, still with a step_size of ONE.
TEST(Position, Position_Fractional_Point) {
  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 44100, 1, 44100,
                           Resampler::SampleAndHold);
  ASSERT_NE(mixer, nullptr);

  //
  // Check: source supply exceeds destination demand
  // Source (offset 1.5 of 5) has 3.5. Destination (offset 1 of 3) wants 2.
  Fixed source_offset = ffl::FromRatio(3, 2);
  int64_t dest_offset = 1;
  int16_t source[] = {1, 0xC, 0x7B, 0x4D2, 0x3039};
  // Mix will accumulate source[1:2,2:3] into accum[1,2]
  float accum[] = {-0x00002000, -0x00017000, -0x000EA000, -0x00929000, -0x05BA0000};
  float expect[] = {-0x00002000, 0x00064000, 0x003E8000, -0x00929000, -0x05BA0000};
  NormalizeInt28ToPipelineBitwidth(accum, std::size(accum));
  NormalizeInt28ToPipelineBitwidth(expect, std::size(expect));

  bool mix_result = mixer->Mix(accum, 3, &dest_offset, source, 5, &source_offset, true);

  EXPECT_FALSE(mix_result);
  EXPECT_EQ(3, dest_offset);
  EXPECT_EQ(Fixed(ffl::FromRatio(7, 2)), source_offset) << std::hex << source_offset.raw_value();
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));

  //
  // Check: Destination demand exceeds source supply
  // Source (offset 2.49 of 4) has 2. Destination (offset 1 of 4) wants 3.
  source_offset = ffl::FromRatio(5, 2) - Fixed::FromRaw(1);
  dest_offset = 1;
  // Mix will move source[2,3] to accum[1,2]
  float expect2[] = {-0x00002000, 0x0007B000, 0x004D2000, -0x00929000, -0x05BA0000};
  NormalizeInt28ToPipelineBitwidth(expect2, std::size(expect2));

  mix_result = mixer->Mix(accum, 4, &dest_offset, source, 4, &source_offset, false);

  EXPECT_TRUE(mix_result);
  EXPECT_EQ(3, dest_offset);
  EXPECT_EQ(Fixed(ffl::FromRatio(9, 2) - Fixed::FromRaw(1)), source_offset)
      << std::hex << source_offset.raw_value();
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect2));
}

void TestRateModulo(Resampler sampler_type) {
  auto mixer =
      SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, 32000, 1, 48000, sampler_type);
  ASSERT_NE(mixer, nullptr);

  float source[] = {0.0f, 0.1f, 0.2f};
  float accum[3];
  auto expected_source_offset = Fixed(2);

  // Without rate_modulo, we expect source_offset to be less than [2/3 * 3].
  auto source_offset = Fixed(0);
  int64_t dest_offset = 0;

  auto& info = mixer->bookkeeping();
  info.step_size = (kOneFrame * 2) / 3;

  mixer->Mix(accum, std::size(accum), &dest_offset, source, std::size(source), &source_offset,
             false);

  EXPECT_TRUE(std::size(accum) == dest_offset);
  EXPECT_LT(source_offset, expected_source_offset);

  // With rate_modulo, source_offset should be exactly 2 (i.e. 2/3 * 3).
  source_offset = Fixed(0);
  dest_offset = 0;

  info.SetRateModuloAndDenominator(Fixed(2).raw_value() - info.step_size.raw_value() * 3, 3);
  info.source_pos_modulo = 0;
  ASSERT_EQ(info.rate_modulo(),
            static_cast<uint64_t>(Fixed(Fixed(2) - (info.step_size * 3)).raw_value()));

  mixer->Mix(accum, std::size(accum), &dest_offset, source, std::size(source), &source_offset,
             false);

  EXPECT_TRUE(std::size(accum) == dest_offset);
  EXPECT_EQ(source_offset, expected_source_offset);
}

// For provided sampler, validate source_pos_modulo for zero/non-zero no rollover.
// Position accounting uses different code when muted, so also run these position tests when muted.
void TestPositionModuloNoRollover(Resampler sampler_type, bool mute = false) {
  auto mixer =
      SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, 44100, 1, 44100, sampler_type);
  ASSERT_NE(mixer, nullptr);

  float accum[3];
  int64_t dest_offset;
  Fixed source_offset;
  float source[4] = {0.0f};

  // For "almost-but-not-rollover" cases, we generate 3 output samples, leaving
  // source and dest at pos 3 and source_pos_modulo at 9999/10000.
  //
  // Case: Zero source_pos_modulo, almost-but-not-rollover.
  dest_offset = 0;
  source_offset = Fixed(0);

  // For clarity, explicitly setting step_size and denominator, even though
  // step_size is initialized to kOneFrame and denominator's 10000 persists.
  auto& info = mixer->bookkeeping();
  info.step_size = kOneFrame;
  info.SetRateModuloAndDenominator(3333, 10000);
  info.source_pos_modulo = 0;
  if (mute) {
    info.gain.SetSourceGain(Gain::kMinGainDb);
  }

  mixer->Mix(accum, std::size(accum), &dest_offset, source, std::size(source), &source_offset,
             false);
  EXPECT_TRUE(std::size(accum) == dest_offset);
  EXPECT_EQ(Fixed(3), source_offset) << std::hex << source_offset.raw_value();
  EXPECT_EQ(9999u, info.source_pos_modulo);

  // Non-zero source_pos_modulo (but rate_modulo is reduced, so same outcome).
  dest_offset = 0;
  source_offset = Fixed(0);

  info.step_size = kOneFrame;
  info.SetRateModuloAndDenominator(3332, 10000);
  info.source_pos_modulo = 3;
  if (mute) {
    info.gain.SetSourceGain(Gain::kMinGainDb);
  }

  mixer->Mix(accum, std::size(accum), &dest_offset, source, std::size(source), &source_offset,
             false);
  EXPECT_TRUE(std::size(accum) == dest_offset);
  EXPECT_EQ(Fixed(3), source_offset) << std::hex << source_offset.raw_value();
  EXPECT_EQ(9999u, info.source_pos_modulo);
}

// For provided sampler, validate source_pos_modulo for zero/non-zero w/rollover.
// Position accounting uses different code when muted, so also run these position tests when muted.
void TestPositionModuloRollover(Resampler sampler_type, bool mute = false) {
  auto mixer =
      SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, 44100, 1, 44100, sampler_type);
  ASSERT_NE(mixer, nullptr);

  float accum[3];
  int64_t dest_offset;
  Fixed source_offset;
  float source[4] = {0.0f};

  // These "barely-rollover" cases generate 2 frames: source/dest pos at 3, source_pos_mod at
  // 0/10000.
  //
  // Case: Zero source_pos_modulo, just-barely-rollover.
  dest_offset = 1;
  source_offset = Fixed(1) - Fixed::FromRaw(1);

  auto& info = mixer->bookkeeping();
  info.step_size = kOneFrame;
  info.SetRateModuloAndDenominator(5000, 10000);
  info.source_pos_modulo = 0;
  if (mute) {
    info.gain.SetSourceGain(Gain::kMinGainDb);
  }

  mixer->Mix(accum, std::size(accum), &dest_offset, source, std::size(source), &source_offset,
             false);
  EXPECT_TRUE(std::size(accum) == dest_offset);
  EXPECT_EQ(Fixed(3), source_offset) << std::hex << source_offset.raw_value();
  EXPECT_EQ(0u, info.source_pos_modulo);

  // Non-zero source_pos_modulo, just-barely-rollover case.
  dest_offset = 1;
  source_offset = Fixed(1) - Fixed::FromRaw(1);

  info.step_size = kOneFrame;
  info.SetRateModuloAndDenominator(3332, 10000);
  info.source_pos_modulo = 3336;
  if (mute) {
    info.gain.SetSourceGain(Gain::kMinGainDb);
  }

  mixer->Mix(accum, std::size(accum), &dest_offset, source, std::size(source), &source_offset,
             false);
  EXPECT_TRUE(std::size(accum) == dest_offset);
  EXPECT_EQ(Fixed(3), source_offset) << std::hex << source_offset.raw_value();
  EXPECT_EQ(0u, info.source_pos_modulo);
}

// For provided sampler, validate source_pos_modulo for early rollover.
// Position accounting uses different code when muted, so also run these position tests when muted.
void TestPositionModuloEarlyRollover(Resampler sampler_type, bool mute = false) {
  auto mixer =
      SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, 44100, 1, 44100, sampler_type);
  ASSERT_NE(mixer, nullptr);

  float accum[3];
  int64_t dest_offset;
  Fixed source_offset;
  float source[3] = {0.0f};

  // Non-zero source_pos_modulo, early-rollover case.
  dest_offset = 0;
  source_offset = Fixed(1);

  auto& info = mixer->bookkeeping();
  info.step_size = kOneFrame - Fixed::FromRaw(1);
  info.SetRateModuloAndDenominator(2, 3);

  info.source_pos_modulo = 2;
  if (mute) {
    info.gain.SetSourceGain(Gain::kMinGainDb);
  }

  mixer->Mix(accum, std::size(accum), &dest_offset, source, std::size(source), &source_offset,
             false);
  EXPECT_EQ(2, dest_offset);
  EXPECT_EQ(Fixed(3), source_offset) << std::hex << source_offset.raw_value();
  EXPECT_EQ(0u, info.source_pos_modulo);
}

// When setting the frac_source_pos to a value that is at the end (or within pos_filter_width) of
// the source buffer, the sampler should not mix additional frames (neither dest_offset nor
// source_offset should be advanced).
void TestLateSourceOffset(Resampler sampler_type) {
  auto mixer =
      SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, 44100, 1, 44100, sampler_type);
  ASSERT_NE(mixer, nullptr);

  if (mixer->pos_filter_width().raw_value() > 0) {
    float source[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    Fixed source_offset = Fixed(std::size(source)) - mixer->pos_filter_width();
    const auto initial_source_offset = source_offset;

    float accum[4] = {0.0f};
    int64_t dest_offset = 0;

    auto& info = mixer->bookkeeping();
    info.step_size = kOneFrame;

    EXPECT_TRUE(mixer->Mix(accum, std::size(accum), &dest_offset, source, std::size(source),
                           &source_offset, false));
    EXPECT_EQ(dest_offset, 0);
    EXPECT_EQ(source_offset, initial_source_offset);
    EXPECT_FLOAT_EQ(accum[0], 0.0f);
  }
}

TEST(Position, LateSourcePosition_Point) { TestLateSourceOffset(Resampler::SampleAndHold); }

// Verify PointSampler filter widths.
TEST(Position, FilterWidth_Point) {
  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::UNSIGNED_8, 1, 48000, 1, 48000,
                           Resampler::SampleAndHold);

  EXPECT_EQ(mixer->pos_filter_width().raw_value(), kHalfFrame.raw_value());
  EXPECT_EQ(mixer->neg_filter_width().raw_value(), kHalfFrame.raw_value() - 1);

  mixer->Reset();

  EXPECT_EQ(mixer->pos_filter_width().raw_value(), kHalfFrame.raw_value());
  EXPECT_EQ(mixer->neg_filter_width().raw_value(), kHalfFrame.raw_value() - 1);
}

}  // namespace media::audio::test
