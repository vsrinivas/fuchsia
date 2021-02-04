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
// Timing (Position) tests
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
  ASSERT_NE(mixer, nullptr);

  //
  // Check: source supply equals destination demand.
  // Source (offset 2 of 5) has 3. Destination (offset 1 of 4) wants 3.
  int32_t frac_src_offset = 2 << kPtsFractionalBits;
  uint32_t dest_offset = 1;
  int16_t source[] = {1, 0xC, 0x7B, 0x4D2, 0x3039};

  // Mix will add source[2,3,4] to accum[1,2,3]
  float accum[] = {-0x00002000, -0x00017000, -0x000EA000, -0x00929000, -0x05BA0000};
  float expect[] = {-0x00002000, 0x00064000, 0x003E8000, 0x02710000, -0x05BA0000};
  NormalizeInt28ToPipelineBitwidth(accum, std::size(accum));
  NormalizeInt28ToPipelineBitwidth(expect, std::size(expect));

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
  NormalizeInt28ToPipelineBitwidth(accum2, std::size(accum2));
  NormalizeInt28ToPipelineBitwidth(expect2, std::size(expect2));

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
  NormalizeInt28ToPipelineBitwidth(expect3, std::size(expect3));

  mix_result =
      mixer->Mix(accum2, 4, &dest_offset, source, 3 << kPtsFractionalBits, &frac_src_offset, false);

  EXPECT_TRUE(mix_result);
  EXPECT_EQ(1u, dest_offset);
  EXPECT_EQ(3 << kPtsFractionalBits, frac_src_offset);
  EXPECT_THAT(accum2, Pointwise(FloatEq(), expect3));
}

// Validate basic (frame-level) position for SampleAndHold resampler.
TEST(Position, Position_Basic_Point) { TestBasicPosition(Resampler::SampleAndHold); }

// For PointSampler, test sample placement when given fractional position.
// Ensure it doesn't touch other buffer sections, regardless of 'accumulate'
// flag. Check when supply > demand and vice versa (we already know = works).
// This test uses fractional lengths/offsets, still with a step_size of ONE.
// TODO(mpuryear): Change frac_src_frames parameter to be (integer) src_frames,
// as number of frames was never intended to be fractional.
TEST(Position, Position_Fractional_Point) {
  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 44100, 1, 44100,
                           Resampler::SampleAndHold);
  ASSERT_NE(mixer, nullptr);

  //
  // Check: source supply exceeds destination demand
  // Source (offset 1.5 of 5) has 3.5. Destination (offset 1 of 3) wants 2.
  int32_t frac_src_offset = 3 << (kPtsFractionalBits - 1);
  uint32_t dest_offset = 1;
  int16_t source[] = {1, 0xC, 0x7B, 0x4D2, 0x3039};
  // Mix will accumulate source[1:2,2:3] into accum[1,2]
  float accum[] = {-0x00002000, -0x00017000, -0x000EA000, -0x00929000, -0x05BA0000};
  float expect[] = {-0x00002000, 0x00064000, 0x003E8000, -0x00929000, -0x05BA0000};
  NormalizeInt28ToPipelineBitwidth(accum, std::size(accum));
  NormalizeInt28ToPipelineBitwidth(expect, std::size(expect));

  bool mix_result =
      mixer->Mix(accum, 3, &dest_offset, source, 5 << kPtsFractionalBits, &frac_src_offset, true);

  EXPECT_FALSE(mix_result);
  EXPECT_EQ(3u, dest_offset);
  EXPECT_EQ(7 << (kPtsFractionalBits - 1), frac_src_offset);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));

  //
  // Check: Destination demand exceeds source supply
  // Source (offset 2.49 of 4) has 2. Destination (offset 1 of 4) wants 3.
  frac_src_offset = (5 << (kPtsFractionalBits - 1)) - 1;
  dest_offset = 1;
  // Mix will move source[2,3] to accum[1,2]
  float expect2[] = {-0x00002000, 0x0007B000, 0x004D2000, -0x00929000, -0x05BA0000};
  NormalizeInt28ToPipelineBitwidth(expect2, std::size(expect2));

  mix_result =
      mixer->Mix(accum, 4, &dest_offset, source, 4 << kPtsFractionalBits, &frac_src_offset, false);

  EXPECT_TRUE(mix_result);
  EXPECT_EQ(3u, dest_offset);
  EXPECT_EQ((9 << (kPtsFractionalBits - 1)) - 1, frac_src_offset);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect2));
}

// When setting the frac_src_pos to a value that is at the end (or within pos_filter_width) of the
// source buffer, the sampler should not mix additional frames (neither dest_offset nor
// frac_src_offset should be advanced).
void TestLateSourceOffset(Resampler sampler_type) {
  auto mixer =
      SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, 44100, 1, 44100, sampler_type);
  ASSERT_NE(mixer, nullptr);

  if (mixer->pos_filter_width().raw_value() > 0) {
    float source[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    int32_t frac_src_offset =
        (std::size(source) << kPtsFractionalBits) - mixer->pos_filter_width().raw_value();
    const auto initial_frac_src_offset = frac_src_offset;

    float accum[4] = {0.0f};
    uint32_t dest_offset = 0;

    auto& info = mixer->bookkeeping();
    info.step_size = Mixer::FRAC_ONE;

    EXPECT_TRUE(mixer->Mix(accum, std::size(accum), &dest_offset, source,
                           std::size(source) << kPtsFractionalBits, &frac_src_offset, false));
    EXPECT_EQ(dest_offset, 0u);
    EXPECT_EQ(frac_src_offset, initial_frac_src_offset);
    EXPECT_FLOAT_EQ(accum[0], 0.0f);
  }
}

TEST(Position, Point_LateSourcePosition) { TestLateSourceOffset(Resampler::SampleAndHold); }

// Verify PointSampler filter widths.
TEST(Position, FilterWidth_Point) {
  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::UNSIGNED_8, 1, 48000, 1, 48000,
                           Resampler::SampleAndHold);
  ASSERT_NE(mixer, nullptr);

  EXPECT_EQ(mixer->pos_filter_width().raw_value(), Mixer::FRAC_HALF);
  EXPECT_EQ(mixer->neg_filter_width().raw_value(), Mixer::FRAC_HALF - 1);

  mixer->Reset();

  EXPECT_EQ(mixer->pos_filter_width().raw_value(), Mixer::FRAC_HALF);
  EXPECT_EQ(mixer->neg_filter_width().raw_value(), Mixer::FRAC_HALF - 1);
}

}  // namespace media::audio::test
