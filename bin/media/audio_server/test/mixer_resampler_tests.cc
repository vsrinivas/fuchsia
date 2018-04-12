// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include "garnet/bin/media/audio_server/test/mixer_tests_shared.h"

namespace media {
namespace audio {
namespace test {

// Convenience abbreviation within this source file to shorten names
using Resampler = media::audio::Mixer::Resampler;

//
// Timing tests
//
// Sync/timing correctness, to the sample level
// Verify correct FROM and TO locations, and quantity. frac_src_frames &
// src_offset are specified in fractional values (fixed 20.12 format).
//
// Each test contains cases that exercise different code paths within the
// samplers.  A mix job's length is limited by the quantities of source data and
// output needed -- whichever is smaller. For this reason, we explicitly note
// places where we check "supply > demand", vs. "demand > supply", vs. "supply
// == demand". We used the PointSampler in earlier tests, so we already know
// "Supply == Demand" works there. When setting up each case, the so-called
// "supply" is determined by src_frames, and src_offset (into those frames).
// Likewise "demand" is determined by dst_frames and dst_offset into dst_frames.
//
// Verify that PointSampler mixes from/to correct buffer locations. Also ensure
// that it doesn't touch other buffer sections, regardless of 'accumulate'.
// This first test uses integer lengths/offsets, and a step_size of ONE.
TEST(Timing, Position_Basic_Point) {
  uint32_t frac_step_size = Mixer::FRAC_ONE;
  bool mix_result;
  MixerPtr mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 24000, 1, 24000,
                               Resampler::SampleAndHold);

  //
  // Check: source supply exceeds destination demand.
  // Source (offset 2 of 5) can supply 3. Destination (offset 1 of 3) wants 2.
  int32_t frac_src_offset = 2 << kPtsFractionalBits;
  uint32_t dst_offset = 1;
  int16_t source[] = {1, 12, 123, 1234, 12345};
  int32_t accum[] = {-2, -23, -234, -2345, -23456};
  // Mix will accumulate src[2,3] into accum[1,2]
  int32_t expect[] = {-2, 100, 1000, -2345, -23456};

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
  int32_t expect2[] = {-2, 1234, 1000, -2345, -23456};

  mix_result =
      mixer->Mix(accum, 4, &dst_offset, source, 4 << kPtsFractionalBits,
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, false);

  EXPECT_TRUE(mix_result);  // True: Mix completed all of src_frames
  EXPECT_EQ(2u, dst_offset);
  EXPECT_EQ(4 << kPtsFractionalBits, frac_src_offset);
  EXPECT_TRUE(CompareBuffers(accum, expect2, fbl::count_of(accum)));
}

// Verify that LinearSampler mixes from and to correct buffer locations.
// Ensure it doesn't touch other buffer sections, regardless of 'accumulate'
// flag. Check scenarios when supply > demand, and vice versa, and ==.
// This first test uses integer lengths/offsets, and a step_size of ONE.
TEST(Timing, Position_Basic_Linear) {
  uint32_t frac_step_size = Mixer::FRAC_ONE;
  bool mix_result;

  MixerPtr mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                               Resampler::LinearInterpolation);

  //
  // Check: source supply equals destination demand.
  // Source (offset 2 of 5) has 3. Destination (offset 1 of 4) wants 3.
  int32_t frac_src_offset = 2 << kPtsFractionalBits;
  uint32_t dst_offset = 1;
  int16_t source[] = {1, 12, 123, 1234, 12345};
  int32_t accum[] = {-2, -23, -234, -2345, -23456};
  // Mix will add src[2,3,4] to accum[1,2,3]
  int32_t expect[] = {-2, 100, 1000, 10000, -23456};

  mix_result =
      mixer->Mix(accum, 4, &dst_offset, source, 5 << kPtsFractionalBits,
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, true);

  //// Judgment call how to EXPECT(mix_result) - we satisfied both src & dst.
  EXPECT_EQ(4u, dst_offset);
  EXPECT_EQ(5 << kPtsFractionalBits, frac_src_offset);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  //
  // Check: source supply exceeds destination demand.
  // Source (offset 0 of 4) has 4. Destination (offset 2 of 4) wants 2.
  frac_src_offset = 0;
  dst_offset = 2;
  int32_t accum2[] = {-2, -23, -234, -2345, -23456};
  // Mix will add src[0,1] to accum[2,3]
  int32_t expect2[] = {-2, -23, -233, -2333, -23456};

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
  int32_t expect3[] = {123, -23, -233, -2333, -23456};

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
TEST(Timing, Position_Fractional_Point) {
  uint32_t frac_step_size = Mixer::FRAC_ONE;
  bool mix_result;
  MixerPtr mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 44100, 1, 44100,
                               Resampler::SampleAndHold);

  //
  // Check: source supply exceeds destination demand
  // Source (offset 1.5 of 4.75) has 3.25. Destination (offset 1 of 3) wants 2.
  int32_t frac_src_offset = 3 << (kPtsFractionalBits - 1);
  uint32_t dst_offset = 1;
  int16_t source[] = {1, 12, 123, 1234, 12345};
  int32_t accum[] = {-2, -23, -234, -2345, -23456};
  // Mix will accumulate source[1:2,2:3] into accum[1,2]
  int32_t expect[] = {-2, -11, -111, -2345, -23456};

  mix_result =
      mixer->Mix(accum, 3, &dst_offset, source, 19 << (kPtsFractionalBits - 2),
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, true);

  EXPECT_FALSE(mix_result);
  EXPECT_EQ(3u, dst_offset);
  EXPECT_EQ(7 << (kPtsFractionalBits - 1), frac_src_offset);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  //
  // Check: Destination demand exceeds source supply
  // Source (offset 2.5 of 4.25) has 1.75. Destination (offset 1 of 4) wants 3.
  frac_src_offset = 5 << (kPtsFractionalBits - 1);
  dst_offset = 1;
  // Mix will move source[2:3,3:4] to accum[1,2]
  int32_t expect2[] = {-2, 123, 1234, -2345, -23456};

  mix_result =
      mixer->Mix(accum, 4, &dst_offset, source, 17 << (kPtsFractionalBits - 2),
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, false);

  EXPECT_TRUE(mix_result);
  EXPECT_EQ(3u, dst_offset);
  EXPECT_EQ(9 << (kPtsFractionalBits - 1), frac_src_offset);
  EXPECT_TRUE(CompareBuffers(accum, expect2, fbl::count_of(accum)));
}

// Verify LinearSampler mixes from/to correct locations, given fractional src
// locations. Ensure it doesn't touch other buffer sections, regardless of
// 'accumulate' flag. Check cases when supply > demand, and vice versa, and =.
// This test uses fractional lengths/offsets, still with a step_size of ONE.
TEST(Timing, Position_Fractional_Linear) {
  uint32_t frac_step_size = Mixer::FRAC_ONE;
  bool mix_result;
  MixerPtr mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                               Resampler::LinearInterpolation);

  //
  // Check: Source supply equals destination demand
  // Source (offset 2.5 of 4.5) has 2.0. Destination (offset 1 of 3) wants 2.
  int32_t frac_src_offset = 5 << (kPtsFractionalBits - 1);
  uint32_t dst_offset = 1;
  int16_t source[] = {1, 123, 234, 456, 789};
  int32_t accum[] = {-234, -345, -678, -2345, -23456};
  // Mix (accumulate) source[2:3,3:4] into accum[1,2].
  int32_t expect[] = {-234, 0, -222, -2345, -23456};

  mix_result =
      mixer->Mix(accum, 3, &dst_offset, source, 9 << (kPtsFractionalBits - 1),
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, true);

  //// Judgment call how to EXPECT(mix_result) - we satisfied both src & dst.
  EXPECT_EQ(3u, dst_offset);
  EXPECT_EQ(9 << (kPtsFractionalBits - 1), frac_src_offset);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  //
  // Check: Source supply exceeds destination demand
  // Source (offset 0.5 of 3) has 2.5. Destination (offset 2 of 4) wants 2.
  frac_src_offset = 1 << (kPtsFractionalBits - 1);  // 0.5
  dst_offset = 2;
  int16_t source2[] = {-1, -11, -124, 1234, 12345};
  int32_t accum2[] = {-222, -333, -234, -2345, -555};
  // Mix (accumulate) source[0:1,1:2] into accum[2,3].
  int32_t expect2[] = {-222, -333, -240, -2413, -555};

  mix_result =
      mixer->Mix(accum2, 4, &dst_offset, source2, 3 << kPtsFractionalBits,
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, true);

  // Less than one frame of the source buffer remains, and we cached the final
  // sample, so mix_result should be TRUE.
  EXPECT_TRUE(mix_result);
  EXPECT_EQ(4u, dst_offset);
  EXPECT_EQ(5 << (kPtsFractionalBits - 1), frac_src_offset);
  EXPECT_TRUE(CompareBuffers(accum2, expect2, fbl::count_of(accum2)));
  // src_offset ended less than 1 from end: src[2] will be cached for next mix.

  //
  // Check: destination demand exceeds source supply
  // Source (offset -0.5 of 2) has 2.5. Destination (offset 1 of 4) wants 3.
  frac_src_offset = -(1 << (kPtsFractionalBits - 1));
  dst_offset = 1;
  // Mix src[2:0,0:1] into accum[1,2].  [1] = (-124:-1), [2] = (-1:-11)
  int32_t expect3[] = {-222, -63, -6, -2413, -555};

  mix_result =
      mixer->Mix(accum2, 4, &dst_offset, source2, 2 << kPtsFractionalBits,
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, false);

  EXPECT_TRUE(mix_result);
  EXPECT_EQ(3u, dst_offset);
  EXPECT_EQ(3 << (kPtsFractionalBits - 1), frac_src_offset);
  EXPECT_TRUE(CompareBuffers(accum2, expect3, fbl::count_of(accum2)));
}

// Test LinearSampler interpolation accuracy, given fractional position.
// Inputs trigger various +/- values that should be rounded each direction.
// Test varies the fractional starting offsets, still with step_size ONE.
TEST(Timing, Interpolation_Values) {
  uint32_t frac_step_size = Mixer::FRAC_ONE;
  bool mix_result;
  MixerPtr mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                               Resampler::LinearInterpolation);

  //
  // Base check: interpolated value is zero.
  // Zero case. src offset 0.5, should mix 50/50
  int32_t frac_src_offset = 1 << (kPtsFractionalBits - 1);  // 0.5
  uint32_t dst_offset = 0;
  int16_t source[] = {-32767, 32767};
  int32_t accum_result = 0xCAFE;  // value will be overwritten
  int32_t expected = 0;

  mix_result =
      mixer->Mix(&accum_result, 1, &dst_offset, source, 2 << kPtsFractionalBits,
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, false);

  // Less than one frame of the source buffer remains, and we cached the final
  // sample, so mix_result should be TRUE.
  EXPECT_TRUE(mix_result);
  EXPECT_EQ(1u, dst_offset);
  EXPECT_EQ(3 << (kPtsFractionalBits - 1), frac_src_offset);
  EXPECT_EQ(expected, accum_result);

  //
  // Check: interpolated result is negative and should round out (down).
  // src offset of 0.5 should lead us to mix the two src samples 50/50
  frac_src_offset = 1 << (kPtsFractionalBits - 1);  // 0.5
  dst_offset = 0;
  int16_t source2[] = {-32768, 32767};
  accum_result = 0xCAFE;  // value will be overwritten
  expected = -1;          // result of -0.5 correctly rounds out, to -1

  mix_result = mixer->Mix(&accum_result, 1, &dst_offset, source2,
                          2 << kPtsFractionalBits, &frac_src_offset,
                          frac_step_size, Gain::kUnityScale, false);

  EXPECT_EQ(3 << (kPtsFractionalBits - 1), frac_src_offset);
  EXPECT_EQ(expected, accum_result);

  //
  // Check: interpolated result is positive and should round out (up).
  // src offset of 0.5 should lead us to mix the two src samples 50/50
  frac_src_offset = 1 << (kPtsFractionalBits - 1);  // 0.5
  dst_offset = 0;
  int16_t source3[] = {-32766, 32767};
  accum_result = 0xCAFE;  // value will be overwritten
  expected = 1;           // result 0.5 rounds "out" to 1

  mix_result = mixer->Mix(&accum_result, 1, &dst_offset, source3,
                          2 << kPtsFractionalBits, &frac_src_offset,
                          frac_step_size, Gain::kUnityScale, false);

  EXPECT_EQ(3 << (kPtsFractionalBits - 1), frac_src_offset);
  EXPECT_EQ(expected, accum_result);

  //
  // Check: interpolated result is positive and should round in (down).
  // src offset of 0.645996 (0xA56) should cause a .354/.646 mixture
  frac_src_offset = 0xA56;
  dst_offset = 0;
  int16_t source4[] = {-32767, 17958};
  accum_result = 0xCAFE;  // value will be overwritten
  expected = 1;           // result 1.151855469 correctly rounds in, to 1

  mix_result = mixer->Mix(&accum_result, 1, &dst_offset, source4,
                          2 << kPtsFractionalBits, &frac_src_offset,
                          frac_step_size, Gain::kUnityScale, false);

  EXPECT_EQ(0x1A56, frac_src_offset);
  EXPECT_EQ(expected, accum_result);

  //
  // Check: interpolated result is negative and should round in (up).
  // src offset of 0.0625 (0x100) should cause a 15-to-1 mixture.
  frac_src_offset = 0x100;
  dst_offset = 0;
  int16_t source5[] = {-2186, 32767};
  accum_result = 0xCAFE;
  expected = -1;  // result −1.4375 "rounds in", to -1.

  mix_result = mixer->Mix(&accum_result, 1, &dst_offset, source5,
                          2 << kPtsFractionalBits, &frac_src_offset,
                          frac_step_size, Gain::kUnityScale, false);

  EXPECT_EQ(0x1100, frac_src_offset);
  EXPECT_EQ(expected, accum_result);
}

// Subset of above, while varying step_size. Interp results should not change:
// it depends on frac_src_pos, not the frac_step_size into/out of that pos.
// dst_offset andfrac_src_offset should continue to advance accurately
TEST(Timing, Interpolation_Rates) {
  bool mix_result;
  MixerPtr mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                               Resampler::LinearInterpolation);

  //
  // Step_size slightly above 1.5 (1 unit more).
  uint32_t frac_step_size = (3u << (kPtsFractionalBits - 1)) + 1u;
  int32_t frac_src_offset = 0xA56;  // 0.645996094
  uint32_t dst_offset = 0;
  int16_t source[] = {-32767, 17958};
  int32_t accum_result = 0xCAFE;
  int32_t expected = 1;  // result 1.151855 correctly rounds in, to 1

  mix_result =
      mixer->Mix(&accum_result, 1, &dst_offset, source, 2 << kPtsFractionalBits,
                 &frac_src_offset, frac_step_size, Gain::kUnityScale, false);

  // We exhausted the entire source buffer, so mix_result should be TRUE.
  EXPECT_TRUE(mix_result);
  EXPECT_EQ(1u, dst_offset);
  EXPECT_EQ(0x2257, frac_src_offset);
  EXPECT_EQ(expected, accum_result);

  //
  // Step_size slightly below 0.5 (2 units less).
  frac_step_size = (1u << (kPtsFractionalBits - 1)) - 2u;
  frac_src_offset = 0x100;
  dst_offset = 0;
  int16_t source2[] = {-2186, 32767};
  accum_result = 0xCAFE;
  expected = -1;  // result −1.4375 "rounds in", to -1.

  mix_result = mixer->Mix(&accum_result, 1, &dst_offset, source2,
                          2 << kPtsFractionalBits, &frac_src_offset,
                          frac_step_size, Gain::kUnityScale, false);

  EXPECT_FALSE(mix_result);
  EXPECT_EQ(1u, dst_offset);
  EXPECT_EQ(0x08FE, frac_src_offset);
  EXPECT_EQ(expected, accum_result);
}

//
// TODO(mpuryear): Test Mixer::Reset() and pos_filter_width()/neg_filter_width()
//

}  // namespace test
}  // namespace audio
}  // namespace media
