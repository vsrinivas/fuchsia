// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include "garnet/bin/media/audio_server/test/mixer_tests_shared.h"

namespace media {
namespace audio {
namespace test {

//
// Gain tests - how does the Gain object respond when given values close to its
// maximum or minimum; does it correctly cache; do values combine to form Unity
// gain. From a data scaling standpoint, is our scaling accurately performed,
// and is it adequately linear? Do our gains and accumulators behave as expected
// when they overflow?
//
// Gain tests using the Gain and AScale objects only
//
// Test the inline function that converts from fixed-point gain to dB.
TEST(Gain, GainScaleToDb) {
  EXPECT_EQ(GainScaleToDb(Gain::kUnityScale), 0.0);
  EXPECT_EQ(GainScaleToDb(Gain::kUnityScale * 10), 20.0);

  EXPECT_GE(GainScaleToDb(Gain::kUnityScale / 100), -40.0 * 1.000001);
  EXPECT_LE(GainScaleToDb(Gain::kUnityScale / 100), -40.0 * 0.999999);

  EXPECT_GE(GainScaleToDb(Gain::kUnityScale >> 1), -6.0206 * 1.000001);
  EXPECT_LE(GainScaleToDb(Gain::kUnityScale >> 1), -6.0206 * 0.999999);
}

// Do renderer and output gains correctly combine to produce unity scaling?
TEST(Gain, Unity) {
  Gain gain;
  Gain::AScale amplitude_scale;

  gain.SetRendererGain(0.0f);
  amplitude_scale = gain.GetGainScale(0.0f);
  EXPECT_EQ(Gain::kUnityScale, amplitude_scale);

  // These positive/negative values should sum to 0.0: UNITY
  gain.SetRendererGain(Gain::kMaxGain / 2);
  amplitude_scale = gain.GetGainScale(-Gain::kMaxGain / 2);
  EXPECT_EQ(Gain::kUnityScale, amplitude_scale);

  // These positive/negative values should sum to 0.0: UNITY
  gain.SetRendererGain(Gain::kMaxGain);
  amplitude_scale = gain.GetGainScale(-Gain::kMaxGain);
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

// System independently limits RendererGain and OutputGain to kMaxGain (+24.0
// dB), intending for their sum to fit into a fixed-point (4.28) container.
// MTWN-70 relates to Gain's statefulness. Does it need this complexity?
TEST(Gain, MaxClamp) {
  Gain gain, expect_gain;
  Gain::AScale amplitude_scale;

  // RendererGain of 2 * kMaxGain is clamped to kMaxGain (+24 dB).
  gain.SetRendererGain(Gain::kMaxGain * 2);
  amplitude_scale = gain.GetGainScale(0.0f);
  EXPECT_EQ(Gain::kMaxScale, amplitude_scale);

  // System limits RendererGain to kMaxGain, even when the sum is less than 0.
  // RenderGain +36dB (clamped to +24dB) plus OutputGain -48dB becomes -24dB.
  gain.SetRendererGain(Gain::kMaxGain * 1.5f);
  amplitude_scale = gain.GetGainScale(-2 * Gain::kMaxGain);
  // A gain_scale value of 0x10270AC represents -24.0dB.
  EXPECT_EQ(0x10270ACu, amplitude_scale);

  // Today system allows OutputGain > 0, which can produce a [Renderer+Output]
  // gain that exceeds 4.28. This is always clamped back down to kMaxGain.
  // TODO(mpuryear): if we limit OutputGain to 0.0 (MTWN-71), change the below
  //
  // This combination (24.05 dB) even fits into 4.24, but clamps to 24.0
  gain.SetRendererGain(Gain::kMaxGain);
  amplitude_scale = gain.GetGainScale(0.05f);
  EXPECT_EQ(Gain::kMaxScale, amplitude_scale);

  // System limits OutputGain to kMaxGain, independent of renderer gain.
  // RendGain = -kMaxGain, OutGain = 1.5*kMaxGain (limited to Max). Expect 0
  gain.SetRendererGain(-Gain::kMaxGain);
  amplitude_scale = gain.GetGainScale(Gain::kMaxGain * 1.5);
  EXPECT_EQ(Gain::kUnityScale, amplitude_scale);
}

// System independently limits RendererGain and OutputGain to kMinGain (-160dB).
// Is scale set to zero, if either (or the combo) is at or below kMinGain?
TEST(Gain, MinMute) {
  Gain gain;
  Gain::AScale amplitude_scale;

  // if OutputGain <= kMinGain, scale must be 0, regardless of RendererGain
  gain.SetRendererGain(-2 * Gain::kMinGain);
  amplitude_scale = gain.GetGainScale(Gain::kMinGain);
  EXPECT_EQ(0u, amplitude_scale);

  // if RendererGain <= kMinGain, scale must be 0, regardless of OutputGain
  gain.SetRendererGain(Gain::kMinGain);
  // TODO(mpuryear): if we fix MTWN-71, setting Output > 0 will cause DCHECK.
  amplitude_scale = gain.GetGainScale(Gain::kMaxGain * 1.2);
  EXPECT_EQ(0u, amplitude_scale);

  // if sum of RendererGain and OutputGain <= kMinGain, scale should be 0
  // Output gain is just slightly above MinGain, and Render takes us below it
  gain.SetRendererGain(-2.0f);
  amplitude_scale = gain.GetGainScale(Gain::kMinGain + 1.0f);
  EXPECT_EQ(0u, amplitude_scale);
}

// Does GetGainScale round appropriately when converting dB into AScale?
// SetRendererGain just saves the given float; GetGainScale produces a
// fixed-point uint32 (4.28 format), truncating (not rounding) in the process.
TEST(Gain, Precision) {
  Gain gain;
  gain.SetRendererGain(-159.99f);
  Gain::AScale amplitude_scale = gain.GetGainScale(0.0f);
  EXPECT_EQ(0x00000002u, amplitude_scale);
  // TODO(mpuryear): when MTWN-73 is fixed, ...2.68 should round up to ...3

  gain.SetRendererGain(-157.696f);
  amplitude_scale = gain.GetGainScale(0.0f);
  EXPECT_EQ(0x00000003u, amplitude_scale);  // 3.499 correctly rounds down to 3

  gain.SetRendererGain(-0.50f);
  amplitude_scale = gain.GetGainScale(0.0f);
  EXPECT_EQ(0x0F1ADF93u, amplitude_scale);  // (future)
  // TODO(mpuryear): when MTWN-73 is fixed, ...F93.8 should round to ...F94

  gain.SetRendererGain(0.0f);
  amplitude_scale = gain.GetGainScale(Gain::kMaxGain);
  EXPECT_EQ(0xFD9539A4u, amplitude_scale);  // FD9539A4.4 correctly rounds down
}

//
// Data scaling tests
//
// These validate the actual scaling of audio data, including overflow and any
// truncation or rounding (above just checks the generation of scale values).
//
// Verify whether per-stream gain interacts linearly with accumulation buffer.
// TODO(mpuryear): when we fix MTWN-82, update our expected values.
TEST(Gain, Scaling_Linearity) {
  int16_t source[] = {3300, 3276, 35, 4, -14, -25, -3276, -3291};
  int32_t accum[8];
  Gain gain;

  // Validate that +20.00 dB leads to exactly 10x in value (within limits)
  //
  // Can a single signal with kMaxGain clip our accumulation buffer?
  // No, but that one stream IS limited to 16-bit values (even after scaling)
  gain.SetRendererGain(20.0f);
  Gain::AScale stream_scale = gain.GetGainScale(0.0f);

  MixerPtr mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 44100, 1, 44100,
                               Mixer::Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        stream_scale);

  int32_t expect[] = {32767, 32760, 350, 40, -140, -250, -32760, -32768};
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  //
  // How precisely linear are our gain stages, mathematically?
  // Validate that -20.00 dB leads to exactly 0.10x in value
  gain.SetRendererGain(-20.0f);
  stream_scale = gain.GetGainScale(0.0f);

  mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 44100, 1, 44100,
                      Mixer::Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        stream_scale);

  int32_t expect2[] = {329, 327, 3, 0, -2, -3, -328, -330};
  EXPECT_TRUE(CompareBuffers(accum, expect2, fbl::count_of(accum)));
}

// How does our Gain respond to very low values? Today during the scaling
// process we shift-right. This is faster than divide but truncates fractional
// vals toward -inf. This means not only that 0.9999 becomes 0, but also that we
// are unable to attenuate negative vals to 0 (even -0.00000001 stays -1).
// In the future, the system should round fractional data values away from 0.
// By "round away from zero", we mean: 1.5 --> 2; -1.5 --> -2; -1.1 --> -1.
TEST(Gain, Scaling_Precision) {
  // TODO(mpuryear): when MTWN-73 is fixed, amend these values
  int16_t source[] = {32767, -32768, -1, 1};  // max/min values
  int32_t accum[4];

  //
  // Today, a gain even slightly less than unity will reduce all positive vals
  Gain::AScale gain_scale = Gain::kUnityScale - 1;
  MixerPtr mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                               Mixer::Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        gain_scale);

  int32_t expect[] = {32766, -32768, -1, 0};
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  //
  // This gain will output non-zero, given a full-scale signal.
  gain_scale = 0x00002001;
  mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                      Mixer::Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, 2, gain_scale);

  int32_t expect2[] = {1, -2, -1, 0};
  EXPECT_TRUE(CompareBuffers(accum, expect2, fbl::count_of(accum)));

  //
  // Today, this gain truncates full-scale to zero.
  gain_scale = 0x00002000;
  mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                      Mixer::Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, 2, gain_scale);

  int32_t expect3[] = {0, -1, -1, 0};
  EXPECT_TRUE(CompareBuffers(accum, expect3, fbl::count_of(accum)));
}

//
// Tests on our multi-stream accumulator -- can values temporarily exceed the
// max or min values for an individual stream; at what value doese the
// accumulator hit its limit, and at that limit does it clamp or rollover?
//
// Can accumulator result exceed the max range of individual streams?
TEST(Gain, Accumulator) {
  int16_t source[] = {32767, -32768};
  int32_t accum[] = {32767, -32768};
  // when mixed, these should exceed the int32 range

  MixerPtr mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                               Mixer::Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, true, fbl::count_of(accum));

  // These values exceed the per-stream range of int16
  int32_t expect[] = {65534, -65536};
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 2, 48000, 2, 48000,
                      Mixer::Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, true, 1);

  // these values even exceed uint16
  int32_t expect2[] = {98301, -98304};
  EXPECT_TRUE(CompareBuffers(accum, expect2, fbl::count_of(accum)));
}

// How does our accumulator behave at its limits? Does it clamp or rollover?
TEST(Gain, Accumulator_Clamp) {
  int16_t source[] = {32767, -32768};
  // if we add these vals, accum SHOULD clamp to int32::max and int32::min
  // Today, our accumulator actually rolls over. Fix the test when it clamps.
  int32_t accum[] = {std::numeric_limits<int32_t>::max() - 32767 + 2,
                     std::numeric_limits<int32_t>::min() + 32768 - 2};

  MixerPtr mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                               Mixer::Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, true, fbl::count_of(accum));

  // TODO(mpuryear): when MTWN-83 is fixed, expect max and min respectively.
  int32_t expect[] = {std::numeric_limits<int32_t>::min() + 1,
                      std::numeric_limits<int32_t>::max() - 1};
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));
}

}  // namespace test
}  // namespace audio
}  // namespace media
