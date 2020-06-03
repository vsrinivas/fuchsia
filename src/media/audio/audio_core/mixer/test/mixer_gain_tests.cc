// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iterator>

#include <fbl/algorithm.h>

#include "src/media/audio/audio_core/mixer/test/audio_result.h"
#include "src/media/audio/audio_core/mixer/test/mixer_tests_shared.h"

using testing::FloatEq;
using testing::Pointwise;

namespace media::audio::test {

// Convenience abbreviation within this source file to shorten names
using Resampler = ::media::audio::Mixer::Resampler;

//
// Data scaling tests
//
// Tests using Gain via a Mixer object, in a mixing environment.
//
// These validate the actual scaling of audio data, including overflow and any
// truncation or rounding (above just checks the generation of scale values).
//
// When doing direct bit-for-bit comparisons in these tests, we must factor in
// the left-shift biasing that is done while converting input data into the
// internal format of our accumulator. For this reason, all "expect" values are
// specified at a higher-than-needed precision of 24-bit, and then normalized
// down to the actual pipeline width.
//
// The 'MixGain' tests involve gain-scaling in the context of mixing (as opposed
// to earlier tests that directly probe the Gain object in isolation).

// Verify whether per-stream gain interacts linearly with accumulation buffer.
TEST(MixGain, Scaling_Linearity) {
  int16_t source[] = {0x0CE4, 0x0CCC, 0x23, 4, -0x0E, -0x19, -0x0CCC, -0x0CDB};
  float accum[8];

  // Validate that +20.00 dB leads to exactly 10x in value (within limits)
  float stream_gain_db = 20.0f;

  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 44100, 1, 44100,
                           Resampler::SampleAndHold);
  DoMix(mixer.get(), source, accum, false, std::size(accum), stream_gain_db);

  float expect[] = {0x080E8000,  0x07FF8000,  0x015E000,   0x00028000,
                    -0x0008C000, -0x000FA000, -0x07FF8000, -0x0808E000};
  NormalizeInt28ToPipelineBitwidth(expect, std::size(expect));
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));

  //
  // How precisely linear are our gain stages, mathematically?
  // Validate that -12.0411998 dB leads to exactly 0.25x in value
  stream_gain_db = -12.0411998f;

  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 44100, 1, 44100,
                      Resampler::SampleAndHold);
  DoMix(mixer.get(), source, accum, false, std::size(accum), stream_gain_db);

  float expect2[] = {0x00339000,  0x00333000,  0x00008C00,  0x00001000,
                     -0x00003800, -0x00006400, -0x00333000, -0x00336C00};
  NormalizeInt28ToPipelineBitwidth(expect2, std::size(expect2));
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect2));
}

// How does our gain scaling respond to scale values close to the limits?
// Using 16-bit inputs, verify the behavior of our Gain object when given the
// closest-to-Unity and closest-to-Mute scale values.
TEST(MixGain, Scaling_Precision) {
  int16_t max_source[] = {0x7FFF, -0x8000};  // max/min 16-bit signed values.
  float accum[2];

  // kMinGainDbUnity is the lowest (furthest-from-Unity) with no observable
  // attenuation on full-scale (i.e. the smallest indistinguishable from Unity).
  // At this gain_scale, audio should be unchanged.
  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                           Resampler::SampleAndHold);
  DoMix(mixer.get(), max_source, accum, false, std::size(accum), AudioResult::kMinGainDbUnity);

  //  At this gain_scale, resulting audio should be unchanged.
  float max_expect1[] = {0x07FFF000, -0x08000000};  // left-shift source by 12.
  NormalizeInt28ToPipelineBitwidth(max_expect1, std::size(max_expect1));
  EXPECT_THAT(accum, Pointwise(FloatEq(), max_expect1));

  // This is the highest (closest-to-Unity) AScale with an observable effect on
  // full-scale (i.e. the largest sub-Unity AScale distinguishable from Unity).
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                      Resampler::SampleAndHold);
  DoMix(mixer.get(), max_source, accum, false, std::size(accum), AudioResult::kMaxGainDbNonUnity);

  // Float32 has 25-bit precision (not 28), hence our min delta is 8 (not 1).
  float max_expect2[] = {0x07FFEFF8, -0x07FFFFF8};
  NormalizeInt28ToPipelineBitwidth(max_expect2, std::size(max_expect2));
  EXPECT_THAT(accum, Pointwise(FloatEq(), max_expect2));

  // kMinGainDbNonMute is the lowest (closest-to-zero) at which audio is not
  // silenced (i.e. the smallest that is distinguishable from Mute).  Although
  // the results may be smaller than we can represent in our 28-bit test data
  // representation, they are still non-zero and thus validate our scalar limit.
  int16_t min_source[] = {1, -1};
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                      Resampler::SampleAndHold);
  DoMix(mixer.get(), min_source, accum, false, std::size(accum), AudioResult::kMinGainDbNonMute);

  // The method used elsewhere in this file for expected result arrays (28-bit
  // fixed-point, normalized into float) cannot precisely express these values.
  // Nonetheless, they are present and non-zero!
  float min_expect[] = {3.051763215e-13, -3.051763215e-13};
  EXPECT_THAT(accum, Pointwise(FloatEq(), min_expect));

  //
  // kMaxGainDbMute is the highest (furthest-from-Mute) scalar that silences
  // full scale data (i.e. the largest AScale that is indistinguishable from
  // Mute). Consider an AScale value corresponding to ever-so-slightly above
  // -160dB: if this increment is small enough, the float32 cannot discern it
  // and treats it as -160dB, our limit for "automatically mute".  Per a mixer
  // optimization, if gain is Mute-equivalent, we skip mixing altogether. This
  // is equivalent to setting 'accumulate' and adding zeroes, so set that flag
  // here and expect no change in the accumulator, even with max inputs.
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                      Resampler::SampleAndHold);
  DoMix(mixer.get(), max_source, accum, true, std::size(accum), AudioResult::kMaxGainDbMute);

  EXPECT_THAT(accum, Pointwise(FloatEq(), min_expect));
}

//
// Tests on our multi-stream accumulator -- can values temporarily exceed the
// max or min values for an individual stream; at what value doese the
// accumulator hit its limit, and at that limit does it clamp or rollover?
//
// Can accumulator result exceed the max range of individual streams?
TEST(MixGain, Accumulator) {
  int16_t source[] = {0x7FFF, -0x8000};
  float accum[] = {0x07FFF000, -0x08000000};
  float expect[] = {0x0FFFE000, -0x10000000};
  float expect2[] = {0x17FFD000, -0x18000000};

  // When mixed, these far exceed any int16 range
  NormalizeInt28ToPipelineBitwidth(accum, std::size(accum));
  NormalizeInt28ToPipelineBitwidth(expect, std::size(expect));
  NormalizeInt28ToPipelineBitwidth(expect2, std::size(expect2));

  // These values exceed the per-stream range of int16
  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                           Resampler::SampleAndHold);
  DoMix(mixer.get(), source, accum, true, std::size(accum));
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));

  // these values even exceed uint16
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 2, 48000, 2, 48000,
                      Resampler::SampleAndHold);
  DoMix(mixer.get(), source, accum, true, 1);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect2));
}

// Our mixer contains an optimization in which it skips mixing operations if it
// detects that gain is below a certain threshold (regardless of "accumulate").
void TestAccumulatorClear(Resampler sampler_type) {
  int16_t source[] = {-32768, 32767, -16384, 16383};
  float accum[] = {-32768, 32767, -16384, 16383};
  float expect[] = {-32768, 32767, -16384, 16383};

  auto mixer =
      SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000, sampler_type);
  // Use a gain guaranteed to silence any signal -- Gain::kMinGainDb.
  DoMix(mixer.get(), source, accum, true, std::size(accum), Gain::kMinGainDb);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));

  // When accumulate = false but gain is sufficiently low, overwriting previous
  // contents is skipped. This should lead to the same results as above.
  mixer =
      SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000, sampler_type);
  DoMix(mixer.get(), source, accum, false, std::size(accum), Gain::kMinGainDb);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Validate the SampleAndHold interpolator for this behavior.
TEST(MixGain, Accumulator_Clear_Point) { TestAccumulatorClear(Resampler::SampleAndHold); }

// Validate the same assertions, with LinearInterpolation interpolator.
TEST(MixGain, Accumulator_Clear_Linear) { TestAccumulatorClear(Resampler::LinearInterpolation); }

}  // namespace media::audio::test
