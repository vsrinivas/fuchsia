// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include "garnet/bin/media/audio_core/mixer/test/audio_result.h"
#include "garnet/bin/media/audio_core/mixer/test/mixer_tests_shared.h"

namespace media {
namespace audio {
namespace test {

// Convenience abbreviation within this source file to shorten names
using Resampler = media::audio::Mixer::Resampler;

//
// Gain tests - how does the Gain object respond when given values close to its
// maximum or minimum; does it correctly cache; do values combine to form Unity
// gain. From a data scaling standpoint, is our scaling accurately performed,
// and is it adequately linear? Do our gains and accumulators behave as expected
// when they overflow?
//
// Gain tests using the Gain and AScale objects only
//
// Test the internally-used inline func that converts AScale gain to dB.
TEST(Gain, GainScaleToDb) {
  // Unity scale is 0.0dB (no change).
  EXPECT_EQ(GainScaleToDb(Gain::kUnityScale), 0.0f);

  // 10x scale-up in amplitude (by definition) is exactly +20.0dB.
  EXPECT_EQ(GainScaleToDb(Gain::kUnityScale * 10.0f), 20.0f);

  // 1/100x scale-down in amplitude (by definition) is exactly -40.0dB.
  EXPECT_EQ(static_cast<float>(GainScaleToDb(Gain::kUnityScale * 0.01f)),
            -40.0f);

  EXPECT_EQ(static_cast<float>(GainScaleToDb(Gain::kUnityScale * 0.5f)),
            -6.020600f);  // 1/2x scale-down by calculation: -6.02059991328..dB.
}

// Do AudioOut and output gains correctly combine to produce unity scaling?
TEST(Gain, Unity) {
  Gain gain;
  Gain::AScale amplitude_scale;

  gain.SetAudioOutGain(0.0f);
  amplitude_scale = gain.GetGainScale(0.0f);
  EXPECT_EQ(Gain::kUnityScale, amplitude_scale);

  // These positive/negative values should sum to 0.0: UNITY
  gain.SetAudioOutGain(Gain::kMaxGainDb / 2);
  amplitude_scale = gain.GetGainScale(-Gain::kMaxGainDb / 2);
  EXPECT_EQ(Gain::kUnityScale, amplitude_scale);

  // These positive/negative values should sum to 0.0: UNITY
  gain.SetAudioOutGain(Gain::kMaxGainDb);
  amplitude_scale = gain.GetGainScale(-Gain::kMaxGainDb);
  EXPECT_EQ(Gain::kUnityScale, amplitude_scale);
}

// Gain caches any previously set AudioOut gain, using it if needed.
// This verifies the default and caching behavior of the Gain object
TEST(Gain, Caching) {
  Gain gain, expect_gain;
  Gain::AScale amplitude_scale, expect_amplitude_scale;

  // Set expect_amplitude_scale to a value that represents -6.0 dB.
  expect_gain.SetAudioOutGain(6.0f);
  expect_amplitude_scale = expect_gain.GetGainScale(-12.0f);

  // If Render gain defaults to 0.0, this represents -6.0 dB too.
  amplitude_scale = gain.GetGainScale(-6.0f);
  EXPECT_EQ(expect_amplitude_scale, amplitude_scale);

  // Now set a different AudioOut gain that will be cached (+3.0)
  gain.SetAudioOutGain(3.0f);
  amplitude_scale = gain.GetGainScale(-3.0f);
  EXPECT_EQ(Gain::kUnityScale, amplitude_scale);

  // If Render gain is cached val of +3, then combo should be Unity.
  amplitude_scale = gain.GetGainScale(-3.0f);
  EXPECT_EQ(Gain::kUnityScale, amplitude_scale);

  // Try another Output gain; with cached +3 this should equate to -6dB.
  amplitude_scale = gain.GetGainScale(-9.0f);
  EXPECT_EQ(expect_amplitude_scale, amplitude_scale);
}

// System independently limits AudioOutGain to kMaxGainDb (24 dB) and OutputGain
// to 0, intending for their sum to fit into a fixed-point (4.28) container.
// MTWN-70 relates to Gain's statefulness. Does it need this complexity?
TEST(Gain, MaxClamp) {
  Gain gain, expect_gain;
  Gain::AScale amplitude_scale;

  // AudioOutGain of 2 * kMaxGainDb is clamped to kMaxGainDb (+24 dB).
  gain.SetAudioOutGain(Gain::kMaxGainDb * 2);
  amplitude_scale = gain.GetGainScale(0.0f);
  EXPECT_EQ(Gain::kMaxScale, amplitude_scale);

  constexpr float kScale24DbDown = 0.0630957344f;
  // System limits AudioOutGain to kMaxGainDb, even when the sum is less than 0.
  // RenderGain +36dB (clamped to +24dB) plus OutputGain -48dB becomes -24dB.
  gain.SetAudioOutGain(Gain::kMaxGainDb * 1.5f);
  amplitude_scale = gain.GetGainScale(-2 * Gain::kMaxGainDb);
  EXPECT_EQ(kScale24DbDown, amplitude_scale);

  // This combination (24.05 dB) would even fit into 4.24, but clamps to 24.0dB.
  gain.SetAudioOutGain(Gain::kMaxGainDb);
  amplitude_scale = gain.GetGainScale(0.05f);
  EXPECT_EQ(Gain::kMaxScale, amplitude_scale);

  // System limits OutputGain to 0, independent of AudioOut gain.
  // RendGain = -kMaxGainDb, OutGain = 1.0 (limited to 0). Expect -kMaxGainDb.
  gain.SetAudioOutGain(-Gain::kMaxGainDb);
  amplitude_scale = gain.GetGainScale(1.0);
  EXPECT_EQ(kScale24DbDown, amplitude_scale);
}

// System independently limits AudioOutGain and OutputGain to kMinGainDb
// (-160dB). Is scale set to zero, if either (or the combo) is at or below
// kMinGainDb?
TEST(Gain, MinMute) {
  Gain gain;
  Gain::AScale amplitude_scale;

  // if OutputGain <= kMinGainDb, scale must be 0, regardless of AudioOutGain
  gain.SetAudioOutGain(-2 * Gain::kMinGainDb);
  amplitude_scale = gain.GetGainScale(Gain::kMinGainDb);
  EXPECT_EQ(0, amplitude_scale);

  // if AudioOutGain <= kMinGainDb, scale must be 0, regardless of OutputGain
  gain.SetAudioOutGain(Gain::kMinGainDb);
  amplitude_scale = gain.GetGainScale(Gain::kMaxGainDb * 1.2);
  EXPECT_EQ(0, amplitude_scale);

  // if sum of AudioOutGain and OutputGain <= kMinGainDb, scale should be 0
  // Output gain is just slightly above MinGain, and Render takes us below it
  gain.SetAudioOutGain(-2.0f);
  amplitude_scale = gain.GetGainScale(Gain::kMinGainDb + 1.0f);
  EXPECT_EQ(0, amplitude_scale);
}

//
// Data scaling tests
//
// These validate the actual scaling of audio data, including overflow and any
// truncation or rounding (above just checks the generation of scale values).
//
// When doing direct bit-for-bit comparisons in these tests, we must factor in
// the left-shift biasing that is done while converting input data into the
// internal format of our accumulator. For this reason, all "expect" values are
// specified at a higher-than-needed precision of 24-bit, and then normalized
// down to the actual pipeline width.

// Verify whether per-stream gain interacts linearly with accumulation buffer.
TEST(Gain, Scaling_Linearity) {
  int16_t source[] = {0x0CE4, 0x0CCC, 0x23, 4, -0x0E, -0x19, -0x0CCC, -0x0CDB};
  float accum[8];
  Gain gain;

  // Validate that +20.00 dB leads to exactly 10x in value (within limits)
  gain.SetAudioOutGain(20.0f);
  Gain::AScale stream_scale = gain.GetGainScale(0.0f);

  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1,
                               44100, 1, 44100, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        stream_scale);

  float expect[] = {0x080E8000,  0x07FF8000,  0x015E000,   0x00028000,
                    -0x0008C000, -0x000FA000, -0x07FF8000, -0x0808E000};
  NormalizeInt28ToPipelineBitwidth(expect, fbl::count_of(expect));
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  //
  // How precisely linear are our gain stages, mathematically?
  // Validate that -12.0411998 dB leads to exactly 0.25x in value
  gain.SetAudioOutGain(-12.0411998f);
  stream_scale = gain.GetGainScale(0.0f);

  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 44100, 1,
                      44100, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        stream_scale);

  float expect2[] = {0x00339000,  0x00333000,  0x00008C00,  0x00001000,
                     -0x00003800, -0x00006400, -0x00333000, -0x00336C00};
  NormalizeInt28ToPipelineBitwidth(expect2, fbl::count_of(expect2));
  EXPECT_TRUE(CompareBuffers(accum, expect2, fbl::count_of(accum)));
}

// How does our gain scaling respond to scale values close to the limits?
// Using 16-bit inputs, verify the behavior of our Gain object when given the
// closest-to-Unity and closest-to-Mute scale values.
TEST(Gain, Scaling_Precision) {
  int16_t max_source[] = {0x7FFF, -0x8000};  // max/min 16-bit signed values.
  float accum[2];

  // kMinUnityScale is the lowest (furthest-from-Unity) with no observable
  // attenuation on full-scale (i.e. the smallest indistinguishable from Unity).
  // At this gain_scale, audio should be unchanged.
  Gain::AScale gain_scale = AudioResult::kMinUnityScale;
  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1,
                               48000, 1, 48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), max_source, accum, false, fbl::count_of(accum),
        gain_scale);

  //  At this gain_scale, resulting audio should be unchanged.
  float max_expect1[] = {0x07FFF000, -0x08000000};  // left-shift source by 12.
  NormalizeInt28ToPipelineBitwidth(max_expect1, fbl::count_of(max_expect1));
  EXPECT_TRUE(CompareBuffers(accum, max_expect1, fbl::count_of(accum)));

  // This is the highest (closest-to-Unity) AScale with an observable effect on
  // full-scale (i.e. the largest sub-Unity AScale distinguishable from Unity).
  gain_scale = AudioResult::kPrevScaleEpsilon;
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1,
                      48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), max_source, accum, false, fbl::count_of(accum),
        gain_scale);

  // Float32 has 25-bit precision (not 28), hence our min delta is 8 (not 1).
  float max_expect2[] = {0x07FFEFF8, -0x07FFFFF8};
  NormalizeInt28ToPipelineBitwidth(max_expect2, fbl::count_of(max_expect2));
  EXPECT_TRUE(CompareBuffers(accum, max_expect2, fbl::count_of(accum)));

  // kPrevMinScaleNonMute is the lowest (closest-to-zero) at which audio is not
  // silenced (i.e. the smallest that is distinguishable from Mute).  Although
  // the results may be smaller than we can represent in our 28-bit test data
  // representation, they are still non-zero and thus validate our scalar limit.
  int16_t min_source[] = {1, -1};
  gain_scale = AudioResult::kPrevMinScaleNonMute;
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1,
                      48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), min_source, accum, false, fbl::count_of(accum),
        gain_scale);

  // The method used elsewhere in this file for expected result arrays (28-bit
  // fixed-point, normalized into float) cannot precisely express these values.
  // Nonetheless, they are present and non-zero!
  float min_expect[] = {3.051758065e-13, -3.051758065e-13};
  EXPECT_TRUE(CompareBuffers(accum, min_expect, fbl::count_of(accum)));

  //
  // kMaxScaleMute is the highest (furthest-from-Mute) scalar that silences full
  // scale data (i.e. the largest AScale that is indistinguishable from Mute).
  // Consider an AScale value corresponding to ever-so-slightly above -160dB: if
  // this increment is small enough, the float32 cannot discern it and treats it
  // as -160dB, our limit for "automatically mute".  Per a mixer optimization,
  // if gain is Mute-equivalent, we skip mixing altogether. This is equivalent
  // to setting the 'accumulate' flag and adding zeroes, so we set that flag
  // here and expect no change in the accumulator, even with max inputs.
  gain_scale = AudioResult::kMaxScaleMute;
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1,
                      48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), max_source, accum, true, fbl::count_of(accum),
        gain_scale);

  EXPECT_TRUE(CompareBuffers(accum, min_expect, fbl::count_of(accum)));
}

//
// Tests on our multi-stream accumulator -- can values temporarily exceed the
// max or min values for an individual stream; at what value doese the
// accumulator hit its limit, and at that limit does it clamp or rollover?
//
// Can accumulator result exceed the max range of individual streams?
TEST(Gain, Accumulator) {
  int16_t source[] = {0x7FFF, -0x8000};
  float accum[] = {0x07FFF000, -0x08000000};
  float expect[] = {0x0FFFE000, -0x10000000};
  float expect2[] = {0x17FFD000, -0x18000000};

  // When mixed, these far exceed any int16 range
  NormalizeInt28ToPipelineBitwidth(accum, fbl::count_of(accum));
  NormalizeInt28ToPipelineBitwidth(expect, fbl::count_of(expect));
  NormalizeInt28ToPipelineBitwidth(expect2, fbl::count_of(expect2));

  // These values exceed the per-stream range of int16
  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1,
                               48000, 1, 48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, true, fbl::count_of(accum));
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  // these values even exceed uint16
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 2, 48000, 2,
                      48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, true, 1);
  EXPECT_TRUE(CompareBuffers(accum, expect2, fbl::count_of(accum)));
}

// Our mixer contains an optimization in which it skips mixing operations if it
// detects that gain is below a certain threshold (regardless of "accumulate").
TEST(Gain, Accumulator_Clear) {
  int16_t source[] = {-32768, 32767};
  float accum[] = {-32768, 32767};
  float expect[] = {-32768, 32767};

  // We will test both SampleAndHold and LinearInterpolation interpolators.
  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1,
                               48000, 1, 48000, Resampler::SampleAndHold);
  // Use the gain guaranteed to silence all signals: Gain::MuteThreshold.
  DoMix(std::move(mixer), source, accum, true, fbl::count_of(accum),
        Gain::MuteThreshold());
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  // Try with the other sampler.
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1,
                      48000, Resampler::LinearInterpolation);
  DoMix(std::move(mixer), source, accum, true, fbl::count_of(accum),
        Gain::MuteThreshold());
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  //
  // When accumulate = false, this is overridden: it behaves identically.
  //
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1,
                      48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        Gain::MuteThreshold());
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  // Ensure that both samplers behave identically in this regard.
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1,
                      48000, Resampler::LinearInterpolation);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        Gain::MuteThreshold());
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));
}

// Headroom - post-SUM gain
// TODO(mpuryear): When we have a master gain stage that can take advantage of
// the headroom inherent in a multi-stream accumulator, implement this test.

}  // namespace test
}  // namespace audio
}  // namespace media
