// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include "garnet/bin/media/audio_server/mixer/test/audio_result.h"
#include "garnet/bin/media/audio_server/mixer/test/mixer_tests_shared.h"

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

// Do renderer and output gains correctly combine to produce unity scaling?
TEST(Gain, Unity) {
  Gain gain;
  Gain::AScale amplitude_scale;

  gain.SetRendererGain(0.0f);
  amplitude_scale = gain.GetGainScale(0.0f);
  EXPECT_EQ(Gain::kUnityScale, amplitude_scale);

  // These positive/negative values should sum to 0.0: UNITY
  gain.SetRendererGain(Gain::kMaxGainDb / 2);
  amplitude_scale = gain.GetGainScale(-Gain::kMaxGainDb / 2);
  EXPECT_EQ(Gain::kUnityScale, amplitude_scale);

  // These positive/negative values should sum to 0.0: UNITY
  gain.SetRendererGain(Gain::kMaxGainDb);
  amplitude_scale = gain.GetGainScale(-Gain::kMaxGainDb);
  EXPECT_EQ(Gain::kUnityScale, amplitude_scale);
}

// Gain caches any previously set Renderer gain, using it if needed.
// This verifies the default and caching behavior of the Gain object
TEST(Gain, Caching) {
  Gain gain, expect_gain;
  Gain::AScale amplitude_scale, expect_amplitude_scale;

  // Set expect_amplitude_scale to a value that represents -6.0 dB.
  expect_gain.SetRendererGain(6.0f);
  expect_amplitude_scale = expect_gain.GetGainScale(-12.0f);

  // If Render gain defaults to 0.0, this represents -6.0 dB too.
  amplitude_scale = gain.GetGainScale(-6.0f);
  EXPECT_EQ(expect_amplitude_scale, amplitude_scale);

  // Now set a different Renderer gain that will be cached (+3.0)
  gain.SetRendererGain(3.0f);
  amplitude_scale = gain.GetGainScale(-3.0f);
  EXPECT_EQ(Gain::kUnityScale, amplitude_scale);

  // If Render gain is cached val of +3, then combo should be Unity.
  amplitude_scale = gain.GetGainScale(-3.0f);
  EXPECT_EQ(Gain::kUnityScale, amplitude_scale);

  // Try another Output gain; with cached +3 this should equate to -6dB.
  amplitude_scale = gain.GetGainScale(-9.0f);
  EXPECT_EQ(expect_amplitude_scale, amplitude_scale);
}

// System independently limits RendererGain to kMaxGainDb (24 dB) and OutputGain
// to 0, intending for their sum to fit into a fixed-point (4.28) container.
// MTWN-70 relates to Gain's statefulness. Does it need this complexity?
TEST(Gain, MaxClamp) {
  Gain gain, expect_gain;
  Gain::AScale amplitude_scale;

  // RendererGain of 2 * kMaxGainDb is clamped to kMaxGainDb (+24 dB).
  gain.SetRendererGain(Gain::kMaxGainDb * 2);
  amplitude_scale = gain.GetGainScale(0.0f);
  EXPECT_EQ(Gain::kMaxScale, amplitude_scale);

  constexpr float kScale24DbDown = 0.0630957344f;
  // System limits RendererGain to kMaxGainDb, even when the sum is less than 0.
  // RenderGain +36dB (clamped to +24dB) plus OutputGain -48dB becomes -24dB.
  gain.SetRendererGain(Gain::kMaxGainDb * 1.5f);
  amplitude_scale = gain.GetGainScale(-2 * Gain::kMaxGainDb);
  EXPECT_EQ(kScale24DbDown, amplitude_scale);

  // This combination (24.05 dB) would even fit into 4.24, but clamps to 24.0dB.
  gain.SetRendererGain(Gain::kMaxGainDb);
  amplitude_scale = gain.GetGainScale(0.05f);
  EXPECT_EQ(Gain::kMaxScale, amplitude_scale);

  // System limits OutputGain to 0, independent of renderer gain.
  // RendGain = -kMaxGainDb, OutGain = 1.0 (limited to 0). Expect -kMaxGainDb.
  gain.SetRendererGain(-Gain::kMaxGainDb);
  amplitude_scale = gain.GetGainScale(1.0);
  EXPECT_EQ(kScale24DbDown, amplitude_scale);
}

// System independently limits RendererGain and OutputGain to kMinGainDb
// (-160dB). Is scale set to zero, if either (or the combo) is at or below
// kMinGainDb?
TEST(Gain, MinMute) {
  Gain gain;
  Gain::AScale amplitude_scale;

  // if OutputGain <= kMinGainDb, scale must be 0, regardless of RendererGain
  gain.SetRendererGain(-2 * Gain::kMinGainDb);
  amplitude_scale = gain.GetGainScale(Gain::kMinGainDb);
  EXPECT_EQ(0, amplitude_scale);

  // if RendererGain <= kMinGainDb, scale must be 0, regardless of OutputGain
  gain.SetRendererGain(Gain::kMinGainDb);
  amplitude_scale = gain.GetGainScale(Gain::kMaxGainDb * 1.2);
  EXPECT_EQ(0, amplitude_scale);

  // if sum of RendererGain and OutputGain <= kMinGainDb, scale should be 0
  // Output gain is just slightly above MinGain, and Render takes us below it
  gain.SetRendererGain(-2.0f);
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
  int32_t accum[8];
  Gain gain;

  // Validate that +20.00 dB leads to exactly 10x in value (within limits)
  gain.SetRendererGain(20.0f);
  Gain::AScale stream_scale = gain.GetGainScale(0.0f);

  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1,
                               44100, 1, 44100, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        stream_scale);

  int32_t expect[] = {0x080E8000,  0x07FF8000,  0x015E000,   0x00028000,
                      -0x0008C000, -0x000FA000, -0x07FF8000, -0x0808E000};
  NormalizeInt28ToPipelineBitwidth(expect, fbl::count_of(expect));
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  //
  // How precisely linear are our gain stages, mathematically?
  // Validate that -12.0411998 dB leads to exactly 0.25x in value
  gain.SetRendererGain(-12.0411998f);
  stream_scale = gain.GetGainScale(0.0f);

  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 44100, 1,
                      44100, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        stream_scale);

  int32_t expect2[] = {0x00339000,  0x00333000,  0x00008C00,  0x00001000,
                       -0x00003800, -0x00006400, -0x00333000, -0x00336C00};
  NormalizeInt28ToPipelineBitwidth(expect2, fbl::count_of(expect2));
  EXPECT_TRUE(CompareBuffers(accum, expect2, fbl::count_of(accum)));
}

// How does our Gain respond to very low values? Today during the scaling
// process, the system should round fractional data values away from 0.
// By "round away from zero", we mean: 1.5 --> 2; -1.5 --> -2; -1.1 --> -1.
TEST(Gain, Scaling_Precision) {
  int16_t source[] = {0x7FFF, -0x8000, -1, 1};  // max/min values
  int32_t accum[4];

  // kMinUnityScale is the lowest (furthest-from-Unity) with no observable
  // attenuation on (i.e. the smallest indistinguishable from Unity).
  // At this gain_scale, audio should be unchanged.
  Gain::AScale gain_scale = AudioResult::kMinUnityScale;
  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1,
                               48000, 1, 48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        gain_scale);

  int32_t expect[] = {0x07FFF000, -0x08000000, -0x00001000, 0x00001000};
  NormalizeInt28ToPipelineBitwidth(expect, fbl::count_of(expect));
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  // The highest (closest-to-Unity) AScale with an observable effect on
  // full-scale (i.e. the largest sub-Unity AScale distinguishable from Unity).
  gain_scale = AudioResult::kPrevScaleEpsilon;
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1,
                      48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        gain_scale);

  expect[0]--;
  expect[1]++;
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  // kPrevMinScaleNonZero is the lowest (closest-to-zero) at which full-scale
  // are not silenced (i.e. the smallest that is distinguishable from Mute).
  // This 'special' scale straddles boundaries: 32767 is reduced to _just_ less
  // than .5 (and rounds in) while -32768 becomes -.50000 (rounding out to -1).
  gain_scale = AudioResult::kPrevMinScaleNonZero;
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1,
                      48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        gain_scale);

  int32_t expect2[] = {0, -1, 0, 0};
  EXPECT_TRUE(CompareBuffers(accum, expect2, fbl::count_of(accum)));

  // kMaxScaleZero is the highest (furthest-from-Mute) AScale that silences
  // full-scale data (i.e. the largest AScale that is indistinguishable from
  // Mute). At this gain, even -32768 is reduced to -.49 (rounding to 0). This
  // mix includes accumulation, thus nothing should change in the accum buffer.
  gain_scale = AudioResult::kMaxScaleZero;
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1,
                      48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, true, fbl::count_of(accum),
        gain_scale);

  EXPECT_TRUE(CompareBuffers(accum, expect2, fbl::count_of(accum)));
}

//
// Tests on our multi-stream accumulator -- can values temporarily exceed the
// max or min values for an individual stream; at what value doese the
// accumulator hit its limit, and at that limit does it clamp or rollover?
//
// Can accumulator result exceed the max range of individual streams?
TEST(Gain, Accumulator) {
  int16_t source[] = {0x7FFF, -0x8000};
  int32_t accum[] = {0x07FFF000, -0x08000000};
  int32_t expect[] = {0x0FFFE000, -0x10000000};
  int32_t expect2[] = {0x17FFD000, -0x18000000};

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

// How does our accumulator behave at its limits? Does it clamp or rollover?
TEST(Gain, Accumulator_Clamp) {
  int16_t source[] = {0x7FFF, -0x8000};
  // This mix will exceed int32 max and min respectively: accum SHOULD clamp.
  int32_t accum[] = {std::numeric_limits<int32_t>::max() -
                         (source[0] << (kAudioPipelineWidth - 16)) + 1,
                     std::numeric_limits<int32_t>::min() -
                         (source[1] << (kAudioPipelineWidth - 16)) - 1};

  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1,
                               48000, 1, 48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, true, fbl::count_of(accum));

  // TODO(mpuryear): when MTWN-83 is fixed, expect max and min (not min & max).
  int32_t expect[] = {std::numeric_limits<int32_t>::min(),
                      std::numeric_limits<int32_t>::max()};
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));
}

// Our mixer contains an optimization in which it skips mixing operations if it
// detects that gain is below a certain threshold (regardless of "accumulate").
TEST(Gain, Accumulator_Clear) {
  int16_t source[] = {-32768, 32767};
  int32_t accum[] = {-32768, 32767};
  int32_t expect[] = {-32768, 32767};

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
