// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
// Verify correct FROM and TO locations, and quantity. frac_src_frames &
// src_offset are specified in fractional values (fixed 19.13 format).
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
// "supply" is determined by src_frames, and src_offset (into those frames).
// Likewise "demand" is determined by dest_frames and dest_offset into
// dest_frames.

// Verify that the samplers mix to/from correct buffer locations. Also ensure
// that they don't touch other buffer sections, regardless of 'accumulate'.
// This first test uses integer lengths/offsets, and a step_size of ONE.
void TestBasicPosition(Resampler samplerType) {
  bool mix_result;

  auto mixer =
      SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000, samplerType);

  //
  // Check: source supply equals destination demand.
  // Source (offset 2 of 5) has 3. Destination (offset 1 of 4) wants 3.
  int32_t frac_src_offset = 2 << kPtsFractionalBits;
  uint32_t dest_offset = 1;
  int16_t source[] = {1, 0xC, 0x7B, 0x4D2, 0x3039};

  // Mix will add source[2,3,4] to accum[1,2,3]
  float accum[] = {-0x00002000, -0x00017000, -0x000EA000, -0x00929000, -0x05BA0000};
  float expect[] = {-0x00002000, 0x00064000, 0x003E8000, 0x02710000, -0x05BA0000};
  NormalizeInt28ToPipelineBitwidth(accum, fbl::count_of(accum));
  NormalizeInt28ToPipelineBitwidth(expect, fbl::count_of(expect));

  mix_result =
      mixer->Mix(accum, 4, &dest_offset, source, 5 << kPtsFractionalBits, &frac_src_offset, true);

  EXPECT_TRUE(mix_result);
  EXPECT_EQ(4u, dest_offset);
  EXPECT_EQ(5 << kPtsFractionalBits, frac_src_offset);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));

  //
  // Check: source supply exceeds destination demand.
  // Source (offset 0 of 4) has 4. Destination (offset 2 of 4) wants 2.
  frac_src_offset = 0;
  dest_offset = 2;
  // Mix will add source[0,1] to accum2[2,3]
  float accum2[] = {-0x00002000, -0x00017000, -0x000EA000, -0x00929000, -0x05BA0000};
  float expect2[] = {-0x00002000, -0x00017000, -0x000E9000, -0x0091D000, -0x05BA0000};
  NormalizeInt28ToPipelineBitwidth(accum2, fbl::count_of(accum2));
  NormalizeInt28ToPipelineBitwidth(expect2, fbl::count_of(expect2));

  mix_result =
      mixer->Mix(accum2, 4, &dest_offset, source, 4 << kPtsFractionalBits, &frac_src_offset, true);

  EXPECT_FALSE(mix_result);
  EXPECT_EQ(4u, dest_offset);
  EXPECT_EQ(2 << kPtsFractionalBits, frac_src_offset);
  EXPECT_THAT(accum2, Pointwise(FloatEq(), expect2));

  //
  // Check: destination demand exceeds source supply.
  // Source (offset 2 of 3) has 1. Destination (offset 0 of 4) wants 4.
  frac_src_offset = 2 << kPtsFractionalBits;
  dest_offset = 0;
  // Mix will move source[2] to accum[0]
  float expect3[] = {0x0007B000, -0x00017000, -0x000E9000, -0x0091D000, -0x05BA0000};
  NormalizeInt28ToPipelineBitwidth(expect3, fbl::count_of(expect3));

  mix_result =
      mixer->Mix(accum2, 4, &dest_offset, source, 3 << kPtsFractionalBits, &frac_src_offset, false);

  EXPECT_TRUE(mix_result);
  EXPECT_EQ(1u, dest_offset);
  EXPECT_EQ(3 << kPtsFractionalBits, frac_src_offset);
  EXPECT_THAT(accum2, Pointwise(FloatEq(), expect3));
}

// Validate basic (frame-level) position for SampleAndHold resampler.
TEST(Resampling, Position_Basic_Point) { TestBasicPosition(Resampler::SampleAndHold); }

// Validate basic (frame-level) position for Linear resampler.
TEST(Resampling, Position_Basic_Linear) { TestBasicPosition(Resampler::LinearInterpolation); }

// For PointSampler, test sample placement when given fractional position.
// Ensure it doesn't touch other buffer sections, regardless of 'accumulate'
// flag. Check when supply > demand and vice versa (we already know = works).
// This test uses fractional lengths/offsets, still with a step_size of ONE.
// TODO(mpuryear): Change frac_src_frames parameter to be (integer) src_frames,
// as number of frames was never intended to be fractional.
TEST(Resampling, Position_Fractional_Point) {
  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 44100, 1, 44100,
                           Resampler::SampleAndHold);

  //
  // Check: source supply exceeds destination demand
  // Source (offset 1.5 of 5) has 3.5. Destination (offset 1 of 3) wants 2.
  int32_t frac_src_offset = 3 << (kPtsFractionalBits - 1);
  uint32_t dest_offset = 1;
  int16_t source[] = {1, 0xC, 0x7B, 0x4D2, 0x3039};
  // Mix will accumulate source[1:2,2:3] into accum[1,2]
  float accum[] = {-0x00002000, -0x00017000, -0x000EA000, -0x00929000, -0x05BA0000};
  float expect[] = {-0x00002000, -0x0000B000, -0x0006F000, -0x00929000, -0x05BA0000};
  NormalizeInt28ToPipelineBitwidth(accum, fbl::count_of(accum));
  NormalizeInt28ToPipelineBitwidth(expect, fbl::count_of(expect));

  bool mix_result =
      mixer->Mix(accum, 3, &dest_offset, source, 5 << kPtsFractionalBits, &frac_src_offset, true);

  EXPECT_FALSE(mix_result);
  EXPECT_EQ(3u, dest_offset);
  EXPECT_EQ(7 << (kPtsFractionalBits - 1), frac_src_offset);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));

  //
  // Check: Destination demand exceeds source supply
  // Source (offset 2.5 of 4) has 1.5. Destination (offset 1 of 4) wants 3.
  frac_src_offset = 5 << (kPtsFractionalBits - 1);
  dest_offset = 1;
  // Mix will move source[2:3,3:4] to accum[1,2]
  float expect2[] = {-0x00002000, 0x0007B000, 0x004D2000, -0x00929000, -0x05BA0000};
  NormalizeInt28ToPipelineBitwidth(expect2, fbl::count_of(expect2));

  mix_result =
      mixer->Mix(accum, 4, &dest_offset, source, 4 << kPtsFractionalBits, &frac_src_offset, false);

  EXPECT_TRUE(mix_result);
  EXPECT_EQ(3u, dest_offset);
  EXPECT_EQ(9 << (kPtsFractionalBits - 1), frac_src_offset);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect2));
}

// Verify LinearSampler mixes from/to correct locations, given fractional src
// locations. Ensure it doesn't touch other buffer sections, regardless of
// 'accumulate' flag. Check cases when supply > demand and vice versa. (Cases
// where supply equals demand are well-covered elsewhere.) This test uses
// fractional offsets, still with a step_size of ONE.
TEST(Resampling, Position_Fractional_Linear) {
  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                           Resampler::LinearInterpolation);

  //
  // Check: Source supply exceeds destination demand
  // Source (offset 0.5 of 3) has 2.5. Destination (offset 2 of 4) wants 2.
  int32_t frac_src_offset = 1 << (kPtsFractionalBits - 1);  // 0.5
  uint32_t dest_offset = 2;
  int16_t source[] = {-1, -0xB, -0x7C, 0x4D2, 0x3039};

  // Mix (accumulate) source[0:1,1:2] into accum[2,3].
  float accum[] = {-0x000DEFA0, -0x0014D840, -0x00017920, 0x0007BFF0, -0x0022BB00};
  float expect[] = {-0x000DEFA0, -0x0014D840, -0x0001D920, 0x000387F0, -0x0022BB00};
  NormalizeInt28ToPipelineBitwidth(accum, fbl::count_of(accum));
  NormalizeInt28ToPipelineBitwidth(expect, fbl::count_of(expect));
  // TODO(mpuryear): round correctly if accumulating fractional result with
  // previous opposite-polarity result. Ideally round -67.5+123 (55.5) to 56.

  bool mix_result =
      mixer->Mix(accum, 4, &dest_offset, source, 3 << kPtsFractionalBits, &frac_src_offset, true);

  // Less than one frame of the source buffer remains, and we cached the final
  // sample, so mix_result should be TRUE.
  EXPECT_TRUE(mix_result);
  EXPECT_EQ(4u, dest_offset);
  EXPECT_EQ(5 << (kPtsFractionalBits - 1), frac_src_offset);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
  // src_offset ended less than 1 from end: src[2] will be cached for next mix.

  //
  // Check: destination demand exceeds source supply
  // Source (offset -0.5 of 2) has 2.5. Destination (offset 1 of 4) wants 3.
  frac_src_offset = -(1 << (kPtsFractionalBits - 1));
  dest_offset = 1;
  // Mix src[2:0,0:1] into accum[1,2].  [1] = (-124:-1), [2] = (-1:-11)
  float expect2[] = {-0x000DEFA0, -0x0003E800, -0x00006000, 0x000387F0, -0x0022BB00};
  NormalizeInt28ToPipelineBitwidth(expect2, fbl::count_of(expect2));

  mix_result =
      mixer->Mix(accum, 4, &dest_offset, source, 2 << kPtsFractionalBits, &frac_src_offset, false);

  EXPECT_TRUE(mix_result);
  EXPECT_EQ(3u, dest_offset);
  EXPECT_EQ(3 << (kPtsFractionalBits - 1), frac_src_offset);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect2));
}

void TestRateModulo(Resampler sampler_type) {
  auto mixer =
      SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, 32000, 1, 48000, sampler_type);

  float source[] = {0.0f, 0.1f, 0.2f};
  float accum[3];
  int32_t expected_frac_src_offset = 2 << kPtsFractionalBits;

  // Without rate_modulo, we expect frac_src_offset to be less than [2/3 * 3].
  int32_t frac_src_offset = 0;
  uint32_t dest_offset = 0;

  auto& info = mixer->bookkeeping();
  info.step_size = (Mixer::FRAC_ONE * 2) / 3;

  mixer->Mix(accum, fbl::count_of(accum), &dest_offset, source,
             fbl::count_of(source) << kPtsFractionalBits, &frac_src_offset, false);

  EXPECT_EQ(fbl::count_of(accum), dest_offset);
  EXPECT_LT(frac_src_offset, expected_frac_src_offset);

  // With rate_modulo, frac_src_offset should be exactly 2 (i.e. 2/3 * 3).
  frac_src_offset = 0;
  dest_offset = 0;

  info.rate_modulo = (2 << kPtsFractionalBits) - (info.step_size * 3);
  info.denominator = 3;
  info.src_pos_modulo = 0;

  mixer->Mix(accum, fbl::count_of(accum), &dest_offset, source,
             fbl::count_of(source) << kPtsFractionalBits, &frac_src_offset, false);

  EXPECT_EQ(fbl::count_of(accum), dest_offset);
  EXPECT_EQ(frac_src_offset, expected_frac_src_offset);
}

// Verify PointSampler correctly incorporates rate_modulo & denominator
// parameters into position and interpolation results.
TEST(Resampling, Rate_Modulo_Point) { TestRateModulo(Resampler::SampleAndHold); }

// Verify LinearSampler correctly incorporates rate_modulo & denominator
// parameters into position and interpolation results.
TEST(Resampling, Rate_Modulo_Linear) { TestRateModulo(Resampler::LinearInterpolation); }

// For provided sampler, validate src_pos_modulo for zero/non-zero no rollover.
void TestPositionModuloNoRollover(Resampler sampler_type, bool mute = false) {
  auto mixer =
      SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, 44100, 1, 44100, sampler_type);

  float accum[3];
  uint32_t dest_offset;
  int32_t frac_src_offset;
  float source[4] = {0.0f};

  // For "almost-but-not-rollover" cases, we generate 3 output samples, leaving
  // source and dest at pos 3 and src_pos_modulo at 9999/10000.
  //
  // Case: Zero src_pos_modulo, almost-but-not-rollover.
  dest_offset = 0;
  frac_src_offset = 0;

  // For clarity, explicitly setting step_size and denominator, even though
  // step_size is auto-initialized to FRAC_ONE and denominator's 10000 persists.
  auto& info = mixer->bookkeeping();
  info.step_size = Mixer::FRAC_ONE;
  info.rate_modulo = 3333;
  info.denominator = 10000;
  info.src_pos_modulo = 0;
  if (mute) {
    info.gain.SetSourceGain(Gain::kMinGainDb);
  }

  mixer->Mix(accum, fbl::count_of(accum), &dest_offset, source,
             fbl::count_of(source) << kPtsFractionalBits, &frac_src_offset, false);
  EXPECT_EQ(fbl::count_of(accum), dest_offset);
  EXPECT_TRUE(3 * Mixer::FRAC_ONE == frac_src_offset);
  EXPECT_EQ(9999u, info.src_pos_modulo);

  // Non-zero src_pos_modulo (but rate_modulo is reduced, so same outcome).
  dest_offset = 0;
  frac_src_offset = 0;

  info.step_size = Mixer::FRAC_ONE;
  info.rate_modulo = 3332;
  info.denominator = 10000;
  info.src_pos_modulo = 3;
  if (mute) {
    info.gain.SetSourceGain(Gain::kMinGainDb);
  }

  mixer->Mix(accum, fbl::count_of(accum), &dest_offset, source,
             fbl::count_of(source) << kPtsFractionalBits, &frac_src_offset, false);
  EXPECT_EQ(fbl::count_of(accum), dest_offset);
  EXPECT_TRUE(3 * Mixer::FRAC_ONE == frac_src_offset);
  EXPECT_EQ(9999u, info.src_pos_modulo);
}

// For provided sampler, validate src_pos_modulo for zero/non-zero w/rollover.
void TestPositionModuloRollover(Resampler sampler_type, bool mute = false) {
  auto mixer =
      SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, 44100, 1, 44100, sampler_type);

  float accum[3];
  uint32_t dest_offset;
  int32_t frac_src_offset;
  float source[4] = {0.0f};

  // For these three "just-barely-rollover" cases, we generate 2 output
  // samples, leaving source and dest pos at 3 but src_pos_modulo at 0/10000.
  //
  // Case: Zero src_pos_modulo, just-barely-rollover.
  dest_offset = 1;
  frac_src_offset = Mixer::FRAC_ONE - 1;

  auto& info = mixer->bookkeeping();
  info.step_size = Mixer::FRAC_ONE;
  info.rate_modulo = 5000;
  info.denominator = 10000;
  info.src_pos_modulo = 0;
  if (mute) {
    info.gain.SetSourceGain(Gain::kMinGainDb);
  }

  mixer->Mix(accum, fbl::count_of(accum), &dest_offset, source,
             fbl::count_of(source) << kPtsFractionalBits, &frac_src_offset, false);
  EXPECT_EQ(fbl::count_of(accum), dest_offset);
  EXPECT_TRUE(3 * Mixer::FRAC_ONE == frac_src_offset);
  EXPECT_EQ(0u, info.src_pos_modulo);

  // Non-zero src_pos_modulo, just-barely-rollover case.
  dest_offset = 1;
  frac_src_offset = Mixer::FRAC_ONE - 1;

  info.step_size = Mixer::FRAC_ONE;
  info.rate_modulo = 3332;
  info.denominator = 10000;
  info.src_pos_modulo = 3336;
  if (mute) {
    info.gain.SetSourceGain(Gain::kMinGainDb);
  }

  mixer->Mix(accum, fbl::count_of(accum), &dest_offset, source,
             fbl::count_of(source) << kPtsFractionalBits, &frac_src_offset, false);
  EXPECT_EQ(fbl::count_of(accum), dest_offset);
  EXPECT_TRUE(3 * Mixer::FRAC_ONE == frac_src_offset);
  EXPECT_EQ(0u, info.src_pos_modulo);
}

// For provided sampler, validate src_pos_modulo for early rollover.
void TestPositionModuloEarlyRolloverPoint(bool mute = false) {
  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, 44100, 1, 44100,
                           Resampler::SampleAndHold);
  float accum[3];
  uint32_t dest_offset;
  int32_t frac_src_offset;
  float source[3] = {0.0f};

  // Non-zero src_pos_modulo, early-rollover case.
  dest_offset = 0;
  frac_src_offset = Mixer::FRAC_ONE - 1;

  auto& info = mixer->bookkeeping();
  info.step_size = Mixer::FRAC_ONE;
  info.rate_modulo = 1;
  info.denominator = 2;
  info.src_pos_modulo = 0;
  if (mute) {
    info.gain.SetSourceGain(Gain::kMinGainDb);
  }

  mixer->Mix(accum, fbl::count_of(accum), &dest_offset, source,
             fbl::count_of(source) << kPtsFractionalBits, &frac_src_offset, false);
  EXPECT_EQ(2u, dest_offset);
  EXPECT_TRUE(3 * Mixer::FRAC_ONE == frac_src_offset);
  EXPECT_EQ(0u, info.src_pos_modulo);
}

// For provided sampler, validate src_pos_modulo for early rollover.
void TestPositionModuloEarlyRolloverLinear(bool mute = false) {
  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, 44100, 1, 44100,
                           Resampler::LinearInterpolation);
  float accum[3];
  uint32_t dest_offset;
  int32_t frac_src_offset;
  float source[3] = {0.0f};

  // Non-zero src_pos_modulo, early-rollover case.
  dest_offset = 0;
  frac_src_offset = 1;

  auto& info = mixer->bookkeeping();
  info.step_size = Mixer::FRAC_ONE - 1;
  info.rate_modulo = 2;
  info.denominator = 3;
  info.src_pos_modulo = 2;
  if (mute) {
    info.gain.SetSourceGain(Gain::kMinGainDb);
  }

  mixer->Mix(accum, fbl::count_of(accum), &dest_offset, source,
             fbl::count_of(source) << kPtsFractionalBits, &frac_src_offset, false);
  EXPECT_EQ(2u, dest_offset);
  EXPECT_TRUE((2 * Mixer::FRAC_ONE) + 1 == frac_src_offset);
  EXPECT_EQ(0u, info.src_pos_modulo);
}

// When setting the frac_src_pos to a value that is at the end (or within pos_filter_width) of the
// source buffer, the sampler should not
void TestLateSourceOffset(Resampler sampler_type) {
  auto mixer =
      SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, 44100, 1, 44100, sampler_type);

  if (mixer->pos_filter_width() > 0) {
    float source[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    int32_t frac_src_offset =
        (fbl::count_of(source) << kPtsFractionalBits) - mixer->pos_filter_width();

    float accum[4] = {0.0f};
    uint32_t dest_offset = 0;

    auto& info = mixer->bookkeeping();
    info.step_size = Mixer::FRAC_ONE;

    mixer->Mix(accum, fbl::count_of(accum), &dest_offset, source,
               fbl::count_of(source) << kPtsFractionalBits, &frac_src_offset, false);
    EXPECT_EQ(dest_offset, 0u);
    EXPECT_EQ(frac_src_offset, static_cast<int32_t>((fbl::count_of(source) << kPtsFractionalBits) -
                                                    mixer->pos_filter_width()));
    EXPECT_FLOAT_EQ(accum[0], 0.0f);
  }
}

// Verify PointSampler correctly incorporates src_pos_modulo (along with
// rate_modulo and denominator) into position and interpolation results.
TEST(Resampling, Position_Modulo_Point) { TestPositionModuloNoRollover(Resampler::SampleAndHold); }

TEST(Resampling, Position_Modulo_Point_Rollover) {
  TestPositionModuloRollover(Resampler::SampleAndHold);
}

TEST(Resampling, Position_Modulo_Point_Early_Rollover) { TestPositionModuloEarlyRolloverPoint(); }

// Verify LinearSampler correctly incorporates src_pos_modulo (along with
// rate_modulo and denominator) into position and interpolation results.
TEST(Resampling, Position_Modulo_Linear) {
  TestPositionModuloNoRollover(Resampler::LinearInterpolation);
}

TEST(Resampling, Position_Modulo_Linear_Rollover) {
  TestPositionModuloRollover(Resampler::LinearInterpolation);
}

TEST(Resampling, Position_Modulo_Linear_Early_Rollover) { TestPositionModuloEarlyRolloverLinear(); }

// Verify PointSampler correctly incorporates src_pos_modulo (along with
// rate_modulo and denominator) into position and interpolation results.
TEST(Resampling, Position_Modulo_Point_Mute) {
  TestPositionModuloNoRollover(Resampler::SampleAndHold, true);
}

TEST(Resampling, Position_Modulo_Point_Mute_Rollover) {
  TestPositionModuloRollover(Resampler::SampleAndHold, true);
}

TEST(Resampling, Position_Modulo_Point_Mute_Early_Rollover) {
  TestPositionModuloEarlyRolloverPoint(true);
}

// Verify LinearSampler correctly incorporates src_pos_modulo (along with
// rate_modulo and denominator) into position and interpolation results.
TEST(Resampling, Position_Modulo_Linear_Mute) {
  TestPositionModuloNoRollover(Resampler::LinearInterpolation, true);
}

TEST(Resampling, Position_Modulo_Linear_Mute_Rollover) {
  TestPositionModuloRollover(Resampler::LinearInterpolation, true);
}

TEST(Resampling, Position_Modulo_Linear_Mute_Early_Rollover) {
  TestPositionModuloEarlyRolloverLinear(true);
}

// Test LinearSampler interpolation accuracy, given fractional position.
// Inputs trigger various +/- values that should be rounded each direction.
//
// With these six precise spot checks, we verify interpolation accuracy to the
// fullest extent possible with 32-bit float and 13-bit subframe timestamps.
void TestLinearInterpolation(uint32_t source_frames_per_second, uint32_t dest_frames_per_second) {
  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, source_frames_per_second, 1,
                           dest_frames_per_second, Resampler::LinearInterpolation);

  //
  // Base check: interpolated value is exactly calculated, no rounding.
  // src offset 0.5, should mix 50/50
  float source1[] = {-1.0f, -0.999999880790710f};  // BF800000, BF7FFFFE
  float expect1 = -0.999999940395355f;             // BF7FFFFF
  uint32_t frac_src_frames = (fbl::count_of(source1)) << kPtsFractionalBits;
  int32_t frac_src_offset = 1 << (kPtsFractionalBits - 1);  // 0x1000 (2000==1)
  uint32_t dest_offset = 0;
  float accum_result = 0xCAFE;  // value will be overwritten

  auto& info = mixer->bookkeeping();
  info.step_size = (static_cast<uint64_t>(source_frames_per_second) << kPtsFractionalBits) /
                   dest_frames_per_second;
  int32_t expected_src_offset = frac_src_offset + info.step_size;

  mixer->Mix(&accum_result, 1, &dest_offset, source1, frac_src_frames, &frac_src_offset, false);
  EXPECT_EQ(dest_offset, 1u);
  EXPECT_EQ(frac_src_offset, expected_src_offset);
  EXPECT_FLOAT_EQ(accum_result, expect1);

  //
  // Additional check: interpolated result is negative and should round out.
  // src offset of 0.25 should lead us to mix the two src samples 75/25, which
  // results in a value -0.999999970197678 that in IEEE-754 format is exactly
  // halfway between the least-significant bit of floating-point precision
  // BF7FFFFF.8). Here, we should round "out" so that this last bit is 0 (the
  // 'round even' convention), so we expect BF800000, which is -1.0.
  expect1 = -1.0f;
  frac_src_offset = 1 << (kPtsFractionalBits - 2);  // 0x0800 (2000==1.0)
  expected_src_offset = frac_src_offset + info.step_size;
  dest_offset = 0;
  accum_result = 0xCAFE;  // Value will be overwritten.

  mixer->Mix(&accum_result, 1, &dest_offset, source1, frac_src_frames, &frac_src_offset, false);
  EXPECT_EQ(dest_offset, 1u);
  EXPECT_EQ(frac_src_offset, expected_src_offset);
  EXPECT_FLOAT_EQ(accum_result, expect1);

  //
  // Base check: interpolated value is exactly calculated, no rounding.
  // src offset 0.5, should mix 50/50
  float source2[] = {0.999999880790710f, 1.0f};  // 3F7FFFFE, 3F800000
  float expect2 = 0.999999940395355f;            // 3F7FFFFF
  frac_src_frames = (fbl::count_of(source2)) << kPtsFractionalBits;
  frac_src_offset = 1 << (kPtsFractionalBits - 1);  // 0x1000 (2000==1.0)
  expected_src_offset = frac_src_offset + info.step_size;
  dest_offset = 0;
  accum_result = 0xCAFE;  // Value will be overwritten.

  mixer->Mix(&accum_result, 1, &dest_offset, source2, frac_src_frames, &frac_src_offset, false);
  EXPECT_EQ(dest_offset, 1u);
  EXPECT_EQ(frac_src_offset, expected_src_offset);
  EXPECT_FLOAT_EQ(accum_result, expect2);

  //
  // Additional check: interpolated result is positive and should round out.
  // src offset of 0x1800 should lead us to mix the two src samples 25/75, which
  // results in a value 0.999999970197678 that in IEEE-754 format is exactly
  // halfway between the least-significant bit of floating-point precision
  // 3F7FFFFF.8). Here, we should round "out" so that this last bit is 0 (the
  // 'round even' convention), so we expect 3F800000, which is +1.0.
  expect2 = 1.0f;
  frac_src_offset = 3 << (kPtsFractionalBits - 2);  // 0x1800 (0x2000==1.0)
  expected_src_offset = frac_src_offset + info.step_size;
  dest_offset = 0;
  accum_result = 0xCAFE;  // Value will be overwritten.

  mixer->Mix(&accum_result, 1, &dest_offset, source2, frac_src_frames, &frac_src_offset, false);
  EXPECT_EQ(dest_offset, 1u);
  EXPECT_EQ(frac_src_offset, expected_src_offset);
  EXPECT_FLOAT_EQ(accum_result, expect2);

  //
  // Check: interpolated result is positive and should round in.
  // src offset 0x17FF (0x2000 is 1.0) should mix just less than 25/75, which
  // results in an interpolated value 0.749694854021072 that in IEEE-754 format
  // is exactly halfway between the least-significant bit of floating-point
  // precision 3F3FEC00.8). Here, we should round "in" so that the LSB is 0 (the
  // 'round even' convention), so we expect 3F3FEC00, which is 0.74969482421875.
  float source3[] = {0.0f, 0.999755859375f};
  float expect3 = 0.74969482421875f;
  frac_src_frames = (fbl::count_of(source3)) << kPtsFractionalBits;
  frac_src_offset = (3 << (kPtsFractionalBits - 2)) - 1;  // 0x17FF (2000==1.0)
  expected_src_offset = frac_src_offset + info.step_size;
  dest_offset = 0;
  accum_result = 0xCAFE;  // Value will be overwritten.

  mixer->Mix(&accum_result, 1, &dest_offset, source3, frac_src_frames, &frac_src_offset, false);
  EXPECT_EQ(dest_offset, 1u);
  EXPECT_EQ(frac_src_offset, expected_src_offset);
  EXPECT_FLOAT_EQ(accum_result, expect3);

  //
  // Check: interpolated result is negative and should round in.
  // src offset of 0x0801, which should mix just less than 75/25, resulting in
  // an interpolated value of -0.749694854021072 that in IEEE-754 format is
  // precisely halfway between the least-significant bit of floating-point
  // precision BF3FEC00.8). Here, we should round "in" so that the LSB is 0 (the
  // 'round even' convention), so we expect BF3FEC00: -0.74969482421875.
  float source4[] = {-0.999755859375f, 0.0f};
  float expect4 = -0.74969482421875f;
  frac_src_frames = (fbl::count_of(source4)) << kPtsFractionalBits;
  frac_src_offset = (1 << (kPtsFractionalBits - 2)) + 1;  // 0x0801 (2000==1.0)
  expected_src_offset = frac_src_offset + info.step_size;
  dest_offset = 0;
  accum_result = 0xCAFE;  // Value will be overwritten.

  mixer->Mix(&accum_result, 1, &dest_offset, source4, frac_src_frames, &frac_src_offset, false);
  EXPECT_EQ(dest_offset, 1u);
  EXPECT_EQ(frac_src_offset, expected_src_offset);
  EXPECT_FLOAT_EQ(accum_result, expect4);
}

// This test varies the fractional starting offsets, still with rate ratio ONE.
TEST(Resampling, Linear_Interp_Values) { TestLinearInterpolation(48000, 48000); }

// Various checks similar to above, while varying rate ratio. Interp results
// should not change: they depend only on frac_src_pos, not the rate ratio.
// dest_offset and frac_src_offset should continue to advance accurately.
//
// Ratios related to the very-common 147:160 conversion.
TEST(Resampling, Linear_Interp_Rate_441_48) {
  TestLinearInterpolation(88200, 48000);
  TestLinearInterpolation(44100, 48000);
}

// Ratios related to the very-common 160:147 conversion.
TEST(Resampling, Linear_Interp_Rate_48_441) {
  TestLinearInterpolation(48000, 44100);
  TestLinearInterpolation(48000, 88200);
}

// Power-of-3 rate ratio 1:3 is guaranteed to have fractional rate error, since
// 1/3 cannot be perfectly represented by a single binary value.
TEST(Resampling, Linear_Interp_Rate_16_48) { TestLinearInterpolation(16000, 48000); }

// Rate change by the smallest-possible increment will be used as micro-SRC, to
// synchronize multiple physically-distinct output devices. This rate ratio also
// has the maximum fractional error when converting to the standard 48000 rate.
TEST(Resampling, Linear_Interp_Rate_MicroSRC) { TestLinearInterpolation(47999, 48000); }

// This rate ratio, when translated into a step_size based on 4096 subframes,
// equates to 3568.999909, generating a maximal fractional value [0.999909].
// Because the callers of Mix() [audio_output and audio_capturer_impl]
// truncate, a maximal fractional value represents maximal fractional error.
TEST(Resampling, Linear_Interp_Rate_Max_Error) { TestLinearInterpolation(38426, 44100); }

// Verify PointSampler filter widths.
TEST(Resampling, FilterWidth_Point) {
  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::UNSIGNED_8, 1, 48000, 1, 48000,
                           Resampler::SampleAndHold);

  EXPECT_EQ(mixer->pos_filter_width(), 0u);
  EXPECT_EQ(mixer->neg_filter_width(), Mixer::FRAC_ONE - 1);

  mixer->Reset();

  EXPECT_EQ(mixer->pos_filter_width(), 0u);
  EXPECT_EQ(mixer->neg_filter_width(), Mixer::FRAC_ONE - 1);
}

// Verify LinearSampler filter widths.
TEST(Resampling, FilterWidth_Linear) {
  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, 44100, 1, 48000,
                           Resampler::LinearInterpolation);

  EXPECT_EQ(mixer->pos_filter_width(), Mixer::FRAC_ONE - 1);
  EXPECT_EQ(mixer->neg_filter_width(), Mixer::FRAC_ONE - 1);

  mixer->Reset();

  EXPECT_EQ(mixer->pos_filter_width(), Mixer::FRAC_ONE - 1);
  EXPECT_EQ(mixer->neg_filter_width(), Mixer::FRAC_ONE - 1);
}

TEST(Resampling, Point_LateSourcePosition) { TestLateSourceOffset(Resampler::SampleAndHold); }

TEST(Resampling, Linear_LateSourcePosition) {
  TestLateSourceOffset(Resampler::LinearInterpolation);
}

// Verify LinearSampler::Reset clears out any cached "previous edge" values.
// Earlier test (Position_Fractional_Linear) already validates that
// LinearSampler correctly caches edge values, so just validate Reset.
TEST(Resampling, Reset_Linear) {
  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                           Resampler::LinearInterpolation);

  // When src_offset ends on fractional val, it caches that sample for next mix
  // Source (offset 0.5 of 3) has 2.5. Destination (offset 2 of 4) wants 2.
  int32_t frac_src_offset = 1 << (kPtsFractionalBits - 1);  // 0.5
  int16_t source[] = {0x1B0, 0xEA, 0x28E, 0x4D2, 0x3039};

  uint32_t dest_offset = 2;
  // Mix (accumulate) source[0:1,1:2] into accum[2,3].
  float accum[] = {-0x0006F000, -0x000DE000, -0x0014D000, -0x001BC000, -0x0022B000};
  float expect[] = {-0x0006F000, -0x000DE000, 0, 0, -0x0022B000};
  NormalizeInt28ToPipelineBitwidth(accum, fbl::count_of(accum));
  NormalizeInt28ToPipelineBitwidth(expect, fbl::count_of(expect));

  EXPECT_TRUE(
      mixer->Mix(accum, 4, &dest_offset, source, 3 << kPtsFractionalBits, &frac_src_offset, true));
  EXPECT_EQ(4u, dest_offset);
  EXPECT_EQ(5 << (kPtsFractionalBits - 1), frac_src_offset);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
  // src_offset ended less than 1 from end: src[2] will be cached for next mix.

  // Mixes with a frac_src_offset < 0 rely on a cached val. This one, post-
  // reset, has no cached vals and hence uses 0 for "left" vals during interp.
  mixer->Reset();

  // Start the src at offset -0.5.
  frac_src_offset = -(1 << (kPtsFractionalBits - 1));
  // Dest wants only one sample, at dest[0].
  dest_offset = 0;
  expect[0] = 0x000D8000;  // Mix(:1B0)=D8 to [0]. W/out Reset, = (28E:1B0)=21F.
  NormalizeInt28ToPipelineBitwidth(&expect[0], 1);

  EXPECT_FALSE(
      mixer->Mix(accum, 1, &dest_offset, source, 2 << kPtsFractionalBits, &frac_src_offset, false));
  EXPECT_EQ(1u, dest_offset);
  EXPECT_EQ(1 << (kPtsFractionalBits - 1), frac_src_offset);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

}  // namespace media::audio::test
