// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>

#include "audio_analysis.h"
#include "mixer_tests_shared.h"

namespace media {
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
// Do renderer and output gains correctly combine to produce unity scaling?
TEST(Gain, UnityGain) {
  audio::Gain gain;
  audio::Gain::AScale amplitude_scale;

  gain.SetRendererGain(0.0f);
  amplitude_scale = gain.GetGainScale(0.0f);
  EXPECT_EQ(audio::Gain::kUnityScale, amplitude_scale);

  // These positive/negative values should sum to 0.0: UNITY
  gain.SetRendererGain(audio::Gain::kMaxGain / 2);
  amplitude_scale = gain.GetGainScale(-audio::Gain::kMaxGain / 2);
  EXPECT_EQ(audio::Gain::kUnityScale, amplitude_scale);

  // These positive/negative values should sum to 0.0: UNITY
  gain.SetRendererGain(audio::Gain::kMaxGain);
  amplitude_scale = gain.GetGainScale(-audio::Gain::kMaxGain);
  EXPECT_EQ(audio::Gain::kUnityScale, amplitude_scale);
}

// Gain caches any previously set Renderer gain, using it if needed.
// This verifies the default and caching behavior of the Gain object
TEST(Gain, GainCaching) {
  audio::Gain gain, expect_gain;
  audio::Gain::AScale amplitude_scale, expect_amplitude_scale;

  // Set expect_amplitude_scale to a value that represents -6.0 dB.
  expect_gain.SetRendererGain(6.0f);
  expect_amplitude_scale = expect_gain.GetGainScale(-12.0f);

  // If Render gain defaults to 0.0, this represents -6.0 dB too.
  amplitude_scale = gain.GetGainScale(-6.0f);
  EXPECT_EQ(expect_amplitude_scale, amplitude_scale);

  // Now set a different Renderer gain that will be cached (+3.0)
  gain.SetRendererGain(3.0f);
  amplitude_scale = gain.GetGainScale(-3.0f);
  EXPECT_EQ(audio::Gain::kUnityScale, amplitude_scale);

  // If Render gain is cached val of +3, then combo should be Unity.
  amplitude_scale = gain.GetGainScale(-3.0f);
  EXPECT_EQ(audio::Gain::kUnityScale, amplitude_scale);

  // Try another Output gain; with cached +3 this should equate to -6dB.
  amplitude_scale = gain.GetGainScale(-9.0f);
  EXPECT_EQ(expect_amplitude_scale, amplitude_scale);
}

// System independently limits RendererGain and OutputGain to kMaxGain (+24.0
// dB), intending for their sum to fit into a fixed-point (4.28) container.
// MTWN-70 relates to audio::Gain's statefulness. Does it need this complexity?
TEST(Gain, MaxGainClamp) {
  audio::Gain gain, expect_gain;
  audio::Gain::AScale amplitude_scale, expect_amplitude_scale;

  // Calculate the expected scale value for our maximum-allowed gain.
  expect_gain.SetRendererGain(audio::Gain::kMaxGain);
  expect_amplitude_scale = expect_gain.GetGainScale(0.0f);

  // RendererGain of 2 * kMaxGain becomes kMaxGain (+24 dB).
  gain.SetRendererGain(audio::Gain::kMaxGain * 2);
  amplitude_scale = gain.GetGainScale(0.0f);
  EXPECT_EQ(expect_amplitude_scale, amplitude_scale);

  // System limits RendererGain to kMaxGain, even if sum is less than zero.
  // Render gain (+24), combined with -48 dB (i.e. -2 * max) becomes -24.
  gain.SetRendererGain(audio::Gain::kMaxGain * 1.5f);
  amplitude_scale = gain.GetGainScale(-2 * audio::Gain::kMaxGain);
  // A gain_scale value of 0x10270AC represents -24.00dB.
  EXPECT_EQ(0x10270ACu, amplitude_scale);

  // Today system allows OutputGain > 0, which can produce a [Renderer+Output]
  // gain that exceeds 4.28. On debug builds, this will DCHECK.
  // TODO(mpuryear): fix the below when we fix MTWN-71 or MTWN-72
  //
  // Rend gain is kMaxGain, so expect 24.05 dB which still fits into 4.24!
  gain.SetRendererGain(audio::Gain::kMaxGain);
  amplitude_scale = gain.GetGainScale(0.05f);
  EXPECT_GT(amplitude_scale, expect_amplitude_scale);

  // System limits OutputGain to kMaxGain, independent of renderer gain.
  // RendGain = -kMaxGain, OutGain = 1.5*kMaxGain (limited to Max). Expect 0
  gain.SetRendererGain(-audio::Gain::kMaxGain);
  amplitude_scale = gain.GetGainScale(audio::Gain::kMaxGain * 1.5);
  EXPECT_EQ(audio::Gain::kUnityScale, amplitude_scale);
}

// System independently limits RendererGain and OutputGain to kMinGain (-160dB).
// Is scale set to zero, if either (or the combo) is at or below kMinGain?
TEST(Gain, MinGainMute) {
  audio::Gain gain;
  audio::Gain::AScale amplitude_scale;

  // if OutputGain <= kMinGain, scale must be 0, regardless of RendererGain
  gain.SetRendererGain(-2 * audio::Gain::kMinGain);
  amplitude_scale = gain.GetGainScale(audio::Gain::kMinGain);
  EXPECT_EQ(0u, amplitude_scale);

  // if RendererGain <= kMinGain, scale must be 0, regardless of OutputGain
  gain.SetRendererGain(audio::Gain::kMinGain);
  // TODO(mpuryear): if we fix MTWN-71, setting Output > 0 will cause DCHECK.
  amplitude_scale = gain.GetGainScale(audio::Gain::kMaxGain * 1.2);
  EXPECT_EQ(0u, amplitude_scale);

  // if sum of RendererGain and OutputGain <= kMinGain, scale should be 0
  // Output gain is just slightly above MinGain, and Render takes us below it
  gain.SetRendererGain(-2.0f);
  amplitude_scale = gain.GetGainScale(audio::Gain::kMinGain + 1.0f);
  EXPECT_EQ(0u, amplitude_scale);
}

// Does GetGainScale round appropriately when converting dB into AScale?
// SetRendererGain just saves the given float; GetGainScale produces a
// fixed-point uint32 (4.28 format), truncating (not rounding) in the process.
TEST(Gain, GainPrecision) {
  audio::Gain gain;
  gain.SetRendererGain(-159.99f);
  audio::Gain::AScale amplitude_scale = gain.GetGainScale(0.0f);
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
  amplitude_scale = gain.GetGainScale(audio::Gain::kMaxGain);
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
TEST(Gain, DataScalingLinearity) {
  std::array<int16_t, 8> src_buf = {3300, 3276, 35, 4, -14, -25, -3276, -3291};
  std::array<int32_t, 8> accum_buf;
  audio::Gain gain;

  // Validate that +20.00 dB leads to exactly 10x in value (within limits)
  //
  // Can a single signal with kMaxGain clip our accumulation buffer?
  // No, but that one stream IS limited to 16-bit values (even after scaling)
  gain.SetRendererGain(20.0f);
  audio::Gain::AScale stream_scale = gain.GetGainScale(0.0f);

  audio::MixerPtr mixer =
      SelectMixer(AudioSampleFormat::SIGNED_16, 1, 44100, 1, 44100);
  DoMix(std::move(mixer), src_buf.data(), accum_buf.data(), false, 8,
        stream_scale);

  std::array<int32_t, 8> expect_buf = {32767, 32760, 350,    40,
                                       -140,  -250,  -32760, -32768};
  EXPECT_TRUE(CompareBuffers<int32_t>(accum_buf.data(), expect_buf.data(), 8));

  //
  // How precisely linear are our gain stages, mathematically?
  // Validate that -20.00 dB leads to exactly 0.10x in value
  gain.SetRendererGain(-20.0f);
  stream_scale = gain.GetGainScale(0.0f);

  mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 44100, 1, 44100);
  DoMix(std::move(mixer), src_buf.data(), accum_buf.data(), false, 8,
        stream_scale);

  expect_buf = {329, 327, 3, 0, -2, -3, -328, -330};
  EXPECT_TRUE(CompareBuffers<int32_t>(accum_buf.data(), expect_buf.data(), 8));
}

// How does our Gain respond to very low values? Today during the scaling
// process we shift-right. This is faster than divide but truncates fractional
// vals toward -inf. This means not only that 0.9999 becomes 0, but also that we
// are unable to attenuate negative vals to 0 (even -0.00000001 stays -1).
// In the future, the system should round fractional data values away from 0.
// By "round away from zero", we mean: 1.5 --> 2; -1.5 --> -2; -1.1 --> -1.
TEST(Gain, DataScalingPrecision) {
  // TODO(mpuryear): when MTWN-73 is fixed, amend these values
  std::array<int16_t, 4> src_buf = {32767, -32768, -1, 1};  // max/min values
  std::array<int32_t, 4> accum_buf;

  //
  // Today, a gain even slightly less than unity will reduce all positive vals
  audio::Gain::AScale gain_scale = audio::Gain::kUnityScale - 1;
  audio::MixerPtr mixer =
      SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000);
  DoMix(std::move(mixer), src_buf.data(), accum_buf.data(), false, 4,
        gain_scale);

  std::array<int32_t, 4> expect_buf = {32766, -32768, -1, 0};
  EXPECT_TRUE(CompareBuffers<int32_t>(accum_buf.data(), expect_buf.data(), 4));

  //
  // This gain will output non-zero, given a full-scale signal.
  gain_scale = 0x00002001;
  mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000);
  DoMix(std::move(mixer), src_buf.data(), accum_buf.data(), false, 2,
        gain_scale);

  expect_buf = {1, -2, -1, 0};
  EXPECT_TRUE(CompareBuffers<int32_t>(accum_buf.data(), expect_buf.data(), 4));

  //
  // Today, this gain truncates full-scale to zero.
  gain_scale = 0x00002000;
  mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000);
  DoMix(std::move(mixer), src_buf.data(), accum_buf.data(), false, 2,
        gain_scale);

  expect_buf = {0, -1, -1, 0};
  EXPECT_TRUE(CompareBuffers<int32_t>(accum_buf.data(), expect_buf.data(), 4));
}

//
// Tests on our multi-stream accumulator -- can values temporarily exceed the
// max or min values for an individual stream; at what value doese the
// accumulator hit its limit, and at that limit does it clamp or rollover?
//
// Can accumulator result exceed the max range of individual streams?
TEST(Gain, Accumulator) {
  std::array<int16_t, 2> src_buf = {32767, -32768};
  std::array<int32_t, 2> accum_buf = {32767, -32768};
  // when mixed, these should exceed the int32 range

  audio::MixerPtr mixer =
      SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000);
  DoMix(std::move(mixer), src_buf.data(), accum_buf.data(), true, 2);

  // These values exceed the per-stream range of int16
  std::array<int32_t, 2> expect_buf = {65534, -65536};
  EXPECT_TRUE(CompareBuffers<int32_t>(accum_buf.data(), expect_buf.data(), 2));

  mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 2, 48000, 2, 48000);
  DoMix(std::move(mixer), src_buf.data(), accum_buf.data(), true, 1);

  // these values even exceed uint16
  expect_buf = {98301, -98304};
  EXPECT_TRUE(CompareBuffers<int32_t>(accum_buf.data(), expect_buf.data(), 2));
}

// How does our accumulator behave at its limits? Does it clamp or rollover?
TEST(Gain, AccumulatorClamp) {
  std::array<int16_t, 2> src_buf = {32767, -32768};
  // if we add these vals, accum SHOULD clamp to int32::max and int32::min
  // Today, our accumulator actually rolls over. Fix the test when it clamps.
  std::array<int32_t, 2> accum_buf = {
      std::numeric_limits<int32_t>::max() - 32767 + 2,
      std::numeric_limits<int32_t>::min() + 32768 - 2};

  audio::MixerPtr mixer =
      SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000);
  DoMix(std::move(mixer), src_buf.data(), accum_buf.data(), true, 2);

  // TODO(mpuryear): when MTWN-83 is fixed, expect max and min respectively.
  std::array<int32_t, 2> expect_buf = {std::numeric_limits<int32_t>::min() + 1,
                                       std::numeric_limits<int32_t>::max() - 1};
  EXPECT_TRUE(CompareBuffers<int32_t>(accum_buf.data(), expect_buf.data(), 2));
}

}  // namespace test
}  // namespace media
