// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include "garnet/bin/media/audio_server/mixer/test/mixer_tests_shared.h"

namespace media {
namespace audio {
namespace test {

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
// Likewise "demand" is determined by dst_frames and dst_offset into dst_frames.

// Verify that PointSampler mixes from/to correct buffer locations. Also ensure
// that it doesn't touch other buffer sections, regardless of 'accumulate'.
// This first test uses integer lengths/offsets, and a step_size of ONE.
TEST(Resampling, Position_Basic_Point) {
  uint32_t frac_step_size = Mixer::FRAC_ONE;
  bool mix_result;
  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1,
                               24000, 1, 24000, Resampler::SampleAndHold);

  //
  // Check: source supply exceeds destination demand.
  // Source (offset 2 of 5) can supply 3. Destination (offset 1 of 3) wants 2.
  int32_t frac_src_offset = 2 << kPtsFractionalBits;
  uint32_t dst_offset = 1;
  int16_t source[] = {1, 0x17, 0x7B, 0x4D2, 0x3039};

  // Mix will accumulate src[2,3] into accum[1,2]
  int32_t accum[] = {-0x200, -0x1700, -0xEA00, -0x92900, -0x5BA000};
  int32_t expect[] = {-0x200, 0x6400, 0x3E800, -0x92900, -0x5BA000};
  NormalizeInt24ToPipelineBitwidth(accum, fbl::count_of(accum));
  NormalizeInt24ToPipelineBitwidth(expect, fbl::count_of(expect));

  mix_result =
      mixer->Mix(accum, 3, &dst_offset, source, 5 << kPtsFractionalBits,
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, true);

  EXPECT_FALSE(mix_result);  // False: Mix did not complete all of src_frames
  EXPECT_EQ(3u, dst_offset);
  EXPECT_EQ(4 << kPtsFractionalBits, frac_src_offset);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  //
  // Check: destination demand exceeds source supply.
  // Source (offset 3 of 4) has 1. Destination (offset 1 of 4) wants 3.
  frac_src_offset = 3 << kPtsFractionalBits;
  dst_offset = 1;
  // Mix will move source[3] into accum[1] (accum==false)
  expect[1] = 0x4D200;
  NormalizeInt24ToPipelineBitwidth(&expect[1], 1);

  mix_result =
      mixer->Mix(accum, 4, &dst_offset, source, 4 << kPtsFractionalBits,
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, false);

  EXPECT_TRUE(mix_result);  // True: Mix completed all of src_frames
  EXPECT_EQ(2u, dst_offset);
  EXPECT_EQ(4 << kPtsFractionalBits, frac_src_offset);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));
}

// Verify that LinearSampler mixes from and to correct buffer locations.
// Ensure it doesn't touch other buffer sections, regardless of 'accumulate'
// flag. Check scenarios when supply > demand, and vice versa, and ==.
// This first test uses integer lengths/offsets, and a step_size of ONE.
TEST(Resampling, Position_Basic_Linear) {
  uint32_t frac_step_size = Mixer::FRAC_ONE;
  bool mix_result;

  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1,
                               48000, 1, 48000, Resampler::LinearInterpolation);

  //
  // Check: source supply equals destination demand.
  // Source (offset 2 of 5) has 3. Destination (offset 1 of 4) wants 3.
  int32_t frac_src_offset = 2 << kPtsFractionalBits;
  uint32_t dst_offset = 1;
  int16_t source[] = {1, 0xC, 0x7B, 0x4D2, 0x3039};
  // Mix will add source[2,3,4] to accum[1,2,3]
  int32_t accum[] = {-0x200, -0x1700, -0xEA00, -0x92900, -0x5BA000};
  int32_t expect[] = {-0x200, 0x6400, 0x3E800, 0x271000, -0x5BA000};
  NormalizeInt24ToPipelineBitwidth(accum, fbl::count_of(accum));
  NormalizeInt24ToPipelineBitwidth(expect, fbl::count_of(expect));

  mix_result =
      mixer->Mix(accum, 4, &dst_offset, source, 5 << kPtsFractionalBits,
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, true);

  EXPECT_TRUE(mix_result);
  EXPECT_EQ(4u, dst_offset);
  EXPECT_EQ(5 << kPtsFractionalBits, frac_src_offset);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  //
  // Check: source supply exceeds destination demand.
  // Source (offset 0 of 4) has 4. Destination (offset 2 of 4) wants 2.
  frac_src_offset = 0;
  dst_offset = 2;
  // Mix will add source[0,1] to accum2[2,3]
  int32_t accum2[] = {-0x200, -0x1700, -0xEA00, -0x92900, -0x5BA000};
  int32_t expect2[] = {-0x200, -0x1700, -0xE900, -0x91D00, -0x5BA000};
  NormalizeInt24ToPipelineBitwidth(accum2, fbl::count_of(accum2));
  NormalizeInt24ToPipelineBitwidth(expect2, fbl::count_of(expect2));

  mix_result =
      mixer->Mix(accum2, 4, &dst_offset, source, 4 << kPtsFractionalBits,
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, true);

  EXPECT_FALSE(mix_result);
  EXPECT_EQ(4u, dst_offset);
  EXPECT_EQ(2 << kPtsFractionalBits, frac_src_offset);
  EXPECT_TRUE(CompareBuffers(accum2, expect2, fbl::count_of(accum2)));

  //
  // Check: destination demand exceeds source supply.
  // Source (offset 2 of 3) has 1. Destination (offset 0 of 4) wants 4.
  frac_src_offset = 2 << kPtsFractionalBits;
  dst_offset = 0;
  // Mix will move source[2] to accum[0]
  int32_t expect3[] = {0x7B00, -0x1700, -0xE900, -0x91D00, -0x5BA000};
  NormalizeInt24ToPipelineBitwidth(expect3, fbl::count_of(expect3));

  mix_result =
      mixer->Mix(accum2, 4, &dst_offset, source, 3 << kPtsFractionalBits,
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, false);

  EXPECT_TRUE(mix_result);
  EXPECT_EQ(1u, dst_offset);
  EXPECT_EQ(3 << kPtsFractionalBits, frac_src_offset);
  EXPECT_TRUE(CompareBuffers(accum2, expect3, fbl::count_of(accum2)));
}

// For PointSampler, test sample placement when given fractional position.
// Ensure it doesn't touch other buffer sections, regardless of 'accumulate'
// flag. Check when supply > demand and vice versa (we already know = works).
// This test uses fractional lengths/offsets, still with a step_size of ONE.
// TODO(mpuryear): Change frac_src_frames parameter to be (integer) src_frames,
// as number of frames was never intended to be fractional.
TEST(Resampling, Position_Fractional_Point) {
  uint32_t frac_step_size = Mixer::FRAC_ONE;
  bool mix_result;
  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1,
                               44100, 1, 44100, Resampler::SampleAndHold);

  //
  // Check: source supply exceeds destination demand
  // Source (offset 1.5 of 5) has 3.5. Destination (offset 1 of 3) wants 2.
  int32_t frac_src_offset = 3 << (kPtsFractionalBits - 1);
  uint32_t dst_offset = 1;
  int16_t source[] = {1, 0xC, 0x7B, 0x4D2, 0x3039};
  // Mix will accumulate source[1:2,2:3] into accum[1,2]
  int32_t accum[] = {-0x200, -0x1700, -0xEA00, -0x92900, -0x5BA000};
  int32_t expect[] = {-0x200, -0xB00, -0x6F00, -0x92900, -0x5BA000};
  NormalizeInt24ToPipelineBitwidth(accum, fbl::count_of(accum));
  NormalizeInt24ToPipelineBitwidth(expect, fbl::count_of(expect));

  mix_result =
      mixer->Mix(accum, 3, &dst_offset, source, 5 << kPtsFractionalBits,
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, true);

  EXPECT_FALSE(mix_result);
  EXPECT_EQ(3u, dst_offset);
  EXPECT_EQ(7 << (kPtsFractionalBits - 1), frac_src_offset);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  //
  // Check: Destination demand exceeds source supply
  // Source (offset 2.5 of 4) has 1.5. Destination (offset 1 of 4) wants 3.
  frac_src_offset = 5 << (kPtsFractionalBits - 1);
  dst_offset = 1;
  // Mix will move source[2:3,3:4] to accum[1,2]
  int32_t expect2[] = {-0x200, 0x7B00, 0x4D200, -0x92900, -0x5BA000};
  NormalizeInt24ToPipelineBitwidth(expect2, fbl::count_of(expect2));

  mix_result =
      mixer->Mix(accum, 4, &dst_offset, source, 4 << kPtsFractionalBits,
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, false);

  EXPECT_TRUE(mix_result);
  EXPECT_EQ(3u, dst_offset);
  EXPECT_EQ(9 << (kPtsFractionalBits - 1), frac_src_offset);
  EXPECT_TRUE(CompareBuffers(accum, expect2, fbl::count_of(accum)));
}

// Verify LinearSampler mixes from/to correct locations, given fractional src
// locations. Ensure it doesn't touch other buffer sections, regardless of
// 'accumulate' flag. Check cases when supply > demand and vice versa. (Cases
// where supply equals demand are well-covered elsewhere.) This test uses
// fractional offsets, still with a step_size of ONE.
TEST(Resampling, Position_Fractional_Linear) {
  uint32_t frac_step_size = Mixer::FRAC_ONE;
  bool mix_result;
  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1,
                               48000, 1, 48000, Resampler::LinearInterpolation);

  //
  // Check: Source supply exceeds destination demand
  // Source (offset 0.5 of 3) has 2.5. Destination (offset 2 of 4) wants 2.
  int32_t frac_src_offset = 1 << (kPtsFractionalBits - 1);  // 0.5
  uint32_t dst_offset = 2;
  int16_t source[] = {-1, -0xB, -0x7C, 0x4D2, 0x3039};

  // Mix (accumulate) source[0:1,1:2] into accum[2,3].
  int32_t accum[] = {-0xDEFA, -0x14D84, -0x1792, 0x7BFF, -0x22BB0};
  int32_t expect[] = {-0xDEFA, -0x14D84, -0x1D92, 0x387F, -0x22BB0};
  NormalizeInt24ToPipelineBitwidth(accum, fbl::count_of(accum));
  NormalizeInt24ToPipelineBitwidth(expect, fbl::count_of(expect));
  // TODO(mpuryear): round correctly if accumulating fractional result with
  // previous opposite-polarity result. Ideally round -67.5+123 (55.5) to 56.

  mix_result =
      mixer->Mix(accum, 4, &dst_offset, source, 3 << kPtsFractionalBits,
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, true);

  // Less than one frame of the source buffer remains, and we cached the final
  // sample, so mix_result should be TRUE.
  EXPECT_TRUE(mix_result);
  EXPECT_EQ(4u, dst_offset);
  EXPECT_EQ(5 << (kPtsFractionalBits - 1), frac_src_offset);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));
  // src_offset ended less than 1 from end: src[2] will be cached for next mix.

  //
  // Check: destination demand exceeds source supply
  // Source (offset -0.5 of 2) has 2.5. Destination (offset 1 of 4) wants 3.
  frac_src_offset = -(1 << (kPtsFractionalBits - 1));
  dst_offset = 1;
  // Mix src[2:0,0:1] into accum[1,2].  [1] = (-124:-1), [2] = (-1:-11)
  int32_t expect2[] = {-0xDEFA, -0x3E80, -0x600, 0x387F, -0x22BB0};
  NormalizeInt24ToPipelineBitwidth(expect2, fbl::count_of(expect2));

  mix_result =
      mixer->Mix(accum, 4, &dst_offset, source, 2 << kPtsFractionalBits,
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, false);

  EXPECT_TRUE(mix_result);
  EXPECT_EQ(3u, dst_offset);
  EXPECT_EQ(3 << (kPtsFractionalBits - 1), frac_src_offset);
  EXPECT_TRUE(CompareBuffers(accum, expect2, fbl::count_of(accum)));
}

void TestPositionModulo(Resampler sampler_type) {
  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1,
                               32000, 1, 48000, sampler_type);

  int16_t source[] = {0, 1, 2};
  uint32_t frac_step_size = (Mixer::FRAC_ONE * 2) / 3;
  int32_t accum[3];
  int32_t expected_frac_src_offset = 2 << kPtsFractionalBits;

  // Without modulo, ending source position should be short of full [2/3 * 2].
  int32_t frac_src_offset = 0;
  uint32_t dst_offset = 0;
  mixer->Mix(accum, fbl::count_of(accum), &dst_offset, source,
             fbl::count_of(source) << kPtsFractionalBits, &frac_src_offset,
             frac_step_size, Gain::kUnityScale, false);

  EXPECT_EQ(fbl::count_of(accum), dst_offset);
  EXPECT_LT(frac_src_offset, expected_frac_src_offset);

  // Now with modulo, source position should be exactly correct.
  frac_src_offset = 0;
  dst_offset = 0;
  uint32_t modulo = (2 << kPtsFractionalBits) - (frac_step_size * 3);
  uint32_t denominator = 3;

  mixer->Mix(accum, fbl::count_of(accum), &dst_offset, source,
             fbl::count_of(source) << kPtsFractionalBits, &frac_src_offset,
             frac_step_size, Gain::kUnityScale, false, modulo, denominator);

  EXPECT_EQ(fbl::count_of(accum), dst_offset);
  EXPECT_EQ(frac_src_offset, expected_frac_src_offset);
}

// Verify PointSampler correctly incorporates modulo & denominator parameters
// into position and interpolation results.
TEST(Resampling, Position_Modulo_Point) {
  TestPositionModulo(Resampler::SampleAndHold);
}

// Verify LinearSampler correctly incorporates modulo & denominator parameters
// into position and interpolation results.
TEST(Resampling, Position_Modulo_Linear) {
  TestPositionModulo(Resampler::LinearInterpolation);
}

// Test LinearSampler interpolation accuracy, given fractional position.
// Inputs trigger various +/- values that should be rounded each direction.
void TestInterpolation(uint32_t source_frames_per_second,
                       uint32_t dest_frames_per_second) {
  bool mix_result;
  MixerPtr mixer = SelectMixer(
      fuchsia::media::AudioSampleFormat::FLOAT, 1, source_frames_per_second, 1,
      dest_frames_per_second, Resampler::LinearInterpolation);

  // These values should lead to [-1,1,0,0] in the accumulator.
  float source[] = {-1.0f / (1 << (kAudioPipelineWidth - 1)),
                    1.0f / (1 << (kAudioPipelineWidth - 1)), 0.0f, 0.0f};
  uint32_t frac_step_size =
      (static_cast<uint64_t>(source_frames_per_second) << kPtsFractionalBits) /
      dest_frames_per_second;

  //
  // Base check: interpolated value is zero.
  // Zero case. src offset 0.5, should mix 50/50
  int32_t frac_src_offset = 1 << (kPtsFractionalBits - 1);  // 0.5
  int32_t expected_src_offset = frac_src_offset + frac_step_size;
  uint32_t dst_offset = 0;
  int32_t accum_result = 0xCAFE;  // value will be overwritten
  int32_t expected = 0;

  mix_result =
      mixer->Mix(&accum_result, 1, &dst_offset, source,
                 (fbl::count_of(source)) << kPtsFractionalBits,
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, false);

  // Less than one frame of the source buffer remains, and we cached the final
  // sample, so mix_result should be TRUE.
  EXPECT_EQ(1u, dst_offset);
  EXPECT_EQ(expected_src_offset, frac_src_offset);
  EXPECT_EQ(expected, accum_result);

  //
  // Check: interpolated result is negative and should round out (down).
  // src offset of 0.25 should lead us to mix the two src samples 75/25
  frac_src_offset = 1 << (kPtsFractionalBits - 2);  // 0.25
  expected_src_offset = frac_src_offset + frac_step_size;
  dst_offset = 0;
  accum_result = 0xCAFE;  // Value will be overwritten.
  expected = -1;          // result -0.5  rounds "out" to -1

  mix_result =
      mixer->Mix(&accum_result, 1, &dst_offset, source, 2 << kPtsFractionalBits,
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, false);

  EXPECT_EQ(1u, dst_offset);
  EXPECT_EQ(expected_src_offset, frac_src_offset);
  EXPECT_EQ(expected, accum_result);

  //
  // Check: interpolated result is positive and should round out (up).
  // src offset of 0.75 should lead us to mix the two src samples 25/75
  frac_src_offset = 3 << (kPtsFractionalBits - 2);  // 0.75
  expected_src_offset = frac_src_offset + frac_step_size;
  dst_offset = 0;
  accum_result = 0xCAFE;  // Value will be overwritten.
  expected = 1;           // result 0.5 rounds "out" to 1

  mix_result =
      mixer->Mix(&accum_result, 1, &dst_offset, source, 2 << kPtsFractionalBits,
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, false);

  EXPECT_EQ(1u, dst_offset);
  EXPECT_EQ(expected_src_offset, frac_src_offset);
  EXPECT_EQ(expected, accum_result);

  //
  // Check: interpolated result is positive and should round in (down).
  // src offset of 0.749755859375 (0xBFF) should mix just less than 25/75.
  frac_src_offset = (3 << (kPtsFractionalBits - 2)) - 1;
  expected_src_offset = frac_src_offset + frac_step_size;
  dst_offset = 0;
  accum_result = 0xCAFE;  // Value will be overwritten.
  expected = 0;           // result 0.49999 rounds "in", to 0.

  mix_result =
      mixer->Mix(&accum_result, 1, &dst_offset, source, 2 << kPtsFractionalBits,
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, false);

  EXPECT_EQ(1u, dst_offset);
  EXPECT_EQ(expected_src_offset, frac_src_offset);
  EXPECT_EQ(expected, accum_result);

  //
  // Check: interpolated result is negative and should round in (up).
  // src offset of 0.250244140625 (0x401) should mix just less than 75/25.
  frac_src_offset = (1 << (kPtsFractionalBits - 2)) + 1;
  expected_src_offset = frac_src_offset + frac_step_size;
  dst_offset = 0;
  accum_result = 0xCAFE;  // Value will be overwritten.
  expected = 0;           // result −0.49999 rounds "in", to 0.

  mix_result =
      mixer->Mix(&accum_result, 1, &dst_offset, source, 2 << kPtsFractionalBits,
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, false);

  EXPECT_EQ(1u, dst_offset);
  EXPECT_EQ(expected_src_offset, frac_src_offset);
  EXPECT_EQ(expected, accum_result);
}

// This test varies the fractional starting offsets, still with rate ratio ONE.
TEST(Resampling, Interpolation_Values) { TestInterpolation(48000, 48000); }

// Various checks similar to above, while varying rate ratio. Interp results
// should not change: they depend only on frac_src_pos, not the rate ratio.
// dst_offset and frac_src_offset should continue to advance accurately.
//
// Ratios related to the very-common 147:160 conversion.
TEST(Resampling, Interpolation_Rate_441_48) {
  TestInterpolation(88200, 48000);
  TestInterpolation(44100, 48000);
}

// Ratios related to the very-common 160:147 conversion.
TEST(Resampling, Interpolation_Rate_48_441) {
  TestInterpolation(48000, 44100);
  TestInterpolation(48000, 88200);
}

// Power-of-3 rate ratio 1:3 is guaranteed to have fractional rate error, since
// 1/3 cannot be perfectly represented by a single binary value.
TEST(Resampling, Interpolation_Rate_16_48) { TestInterpolation(16000, 48000); }

// Rate change by the smallest-possible increment will be used as micro-SRC, to
// synchronize multiple physically-distinct output devices. This rate ratio also
// has the maximum fractional error when converting to the standard 48000 rate.
TEST(Resampling, Interpolation_Rate_MicroSRC) {
  TestInterpolation(47999, 48000);
}

// This rate ratio, when translated into a step_size based on 4096 subframes,
// equates to 3568.999909, generating a maximal fractional value [0.999909].
// Because the callers of Mix() [standard_output_base and audio_capturer_impl]
// truncate, a maximal fractional value represents maximal fractional error.
TEST(Resampling, Interpolation_Rate_Max_Error) {
  TestInterpolation(38426, 44100);
}

// Verify PointSampler filter widths.
TEST(Resampling, FilterWidth_Point) {
  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::UNSIGNED_8, 1,
                               48000, 1, 48000, Resampler::SampleAndHold);

  EXPECT_EQ(mixer->pos_filter_width(), 0u);
  EXPECT_EQ(mixer->neg_filter_width(), Mixer::FRAC_ONE - 1);

  mixer->Reset();

  EXPECT_EQ(mixer->pos_filter_width(), 0u);
  EXPECT_EQ(mixer->neg_filter_width(), Mixer::FRAC_ONE - 1);
}

// Verify LinearSampler filter widths.
TEST(Resampling, FilterWidth_Linear) {
  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1,
                               44100, 1, 48000, Resampler::LinearInterpolation);

  EXPECT_EQ(mixer->pos_filter_width(), Mixer::FRAC_ONE - 1);
  EXPECT_EQ(mixer->neg_filter_width(), Mixer::FRAC_ONE - 1);

  mixer->Reset();

  EXPECT_EQ(mixer->pos_filter_width(), Mixer::FRAC_ONE - 1);
  EXPECT_EQ(mixer->neg_filter_width(), Mixer::FRAC_ONE - 1);
}

// Verify LinearSampler::Reset clears out any cached "previous edge" values.
// Earlier test (Position_Fractional_Linear) already validates
// that LinearSampler correctly caches edge values, so just validate Reset.
TEST(Resampling, Reset_Linear) {
  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1,
                               48000, 1, 48000, Resampler::LinearInterpolation);

  // When src_offset ends on fractional val, it caches that sample for next mix
  // Source (offset 0.5 of 3) has 2.5. Destination (offset 2 of 4) wants 2.
  int32_t frac_src_offset = 1 << (kPtsFractionalBits - 1);  // 0.5
  int16_t source[] = {0x1B0, 0xEA, 0x28E, 0x4D2, 0x3039};

  uint32_t dst_offset = 2;
  uint32_t frac_step_size = Mixer::FRAC_ONE;
  // Mix (accumulate) source[0:1,1:2] into accum[2,3].
  int32_t accum[] = {-0x6F00, -0xDE00, -0x14D00, -0x1BC00, -0x22B00};
  int32_t expect[] = {-0x6F00, -0xDE00, 0, 0, -0x22B00};
  NormalizeInt24ToPipelineBitwidth(accum, fbl::count_of(accum));
  NormalizeInt24ToPipelineBitwidth(expect, fbl::count_of(expect));

  bool mix_result =
      mixer->Mix(accum, 4, &dst_offset, source, 3 << kPtsFractionalBits,
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, true);
  EXPECT_EQ(4u, dst_offset);
  EXPECT_EQ(5 << (kPtsFractionalBits - 1), frac_src_offset);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));
  // src_offset ended less than 1 from end: src[2] will be cached for next mix.

  // Mixes with a frac_src_offset < 0 rely on a cached val. This one, post-
  // reset, has no cached vals and hence uses 0 for "left" vals during interp.
  mixer->Reset();

  // Start the src at offset -0.5.
  frac_src_offset = -(1 << (kPtsFractionalBits - 1));
  // Dst wants only one sample, at dst[0].
  dst_offset = 0;
  expect[0] = 0xD800;  // Mix( :1B0)=D8 to [0]. Without Reset, = (28E:1B0)=21F.
  NormalizeInt24ToPipelineBitwidth(&expect[0], 1);

  mix_result =
      mixer->Mix(accum, 1, &dst_offset, source, 2 << kPtsFractionalBits,
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, false);
  EXPECT_EQ(1u, dst_offset);
  EXPECT_EQ(1 << (kPtsFractionalBits - 1), frac_src_offset);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));
}

}  // namespace test
}  // namespace audio
}  // namespace media
