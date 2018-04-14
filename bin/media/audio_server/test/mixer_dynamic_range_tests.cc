// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/test/audio_result.h"
#include "garnet/bin/media/audio_server/test/mixer_tests_shared.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {
namespace test {

// Convenience abbreviation within this source file to shorten names
using Resampler = media::audio::Mixer::Resampler;

// Ideal dynamic range measurement is exactly equal to the reduction in gain.
// Ideal accompanying noise is ideal noise floor, minus the reduction in gain.
void MeasureSummaryDynamicRange(Gain::AScale scale,
                                double* level_db,
                                double* sinad_db) {
  MixerPtr mixer = SelectMixer(AudioSampleFormat::FLOAT, 1, 48000, 1, 48000,
                               Resampler::SampleAndHold);
  constexpr double amplitude = 1.0;
  constexpr double expected_amplitude =
      amplitude * -std::numeric_limits<int16_t>::min();

  std::vector<float> source(kFreqTestBufSize);
  std::vector<int32_t> accum(kFreqTestBufSize);

  // Populate source buffer; mix it (pass-thru) to accumulation buffer
  OverwriteCosine(source.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq,
                  amplitude);

  uint32_t dst_offset = 0;
  int32_t frac_src_offset = 0;
  mixer->Mix(accum.data(), kFreqTestBufSize, &dst_offset, source.data(),
             kFreqTestBufSize << kPtsFractionalBits, &frac_src_offset,
             Mixer::FRAC_ONE, scale, false);
  EXPECT_EQ(kFreqTestBufSize, dst_offset);
  EXPECT_EQ(static_cast<int32_t>(kFreqTestBufSize << kPtsFractionalBits),
            frac_src_offset);

  // Copy result to double-float buffer, FFT (freq-analyze) it at high-res
  double magn_signal, magn_other;
  MeasureAudioFreq(accum.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq,
                   &magn_signal, &magn_other);

  *level_db = ValToDb(magn_signal / expected_amplitude);
  *sinad_db = ValToDb(magn_signal / magn_other);
}

// Measure dynamic range at two gain settings: less than 1.0 by the smallest
// increment possible, as well as the smallest increment detectable (the
// closest-to-1.0 gain that actually causes incoming data values to change).
TEST(DynamicRange, Epsilon) {
  double unity_level_db, unity_sinad_db, level_db, sinad_db;

  MeasureSummaryDynamicRange(Gain::kUnityScale, &unity_level_db,
                             &unity_sinad_db);
  EXPECT_GE(unity_level_db, 0.0 - AudioResult::kLevelToleranceSourceFloat);
  EXPECT_LE(unity_level_db, 0.0 + AudioResult::kLevelToleranceSourceFloat);
  EXPECT_GE(unity_sinad_db, AudioResult::kPrevFloorSourceFloat);

  // Accumulator has < 28 precision bits, so kScaleEpsilon must < 0x0FFFFFFF.
  static_assert(AudioResult::kScaleEpsilon < Gain::kUnityScale - 1,
                "kScaleEpsilon should be less than kUnityScale - 1");

  // Just above the 'first detectable reduction' scale; should be same as unity.
  MeasureSummaryDynamicRange(AudioResult::kScaleEpsilon + 1, &level_db,
                             &sinad_db);
  EXPECT_EQ(level_db, unity_level_db);
  EXPECT_EQ(sinad_db, unity_sinad_db);

  // kScaleEpsilon:the nearest-unity scale at which we observe effect on inputs.
  // At this 'detectable reduction' scale, level and noise floor appear reduced.
  MeasureSummaryDynamicRange(AudioResult::kScaleEpsilon,
                             &AudioResult::LevelEpsilonDown,
                             &AudioResult::SinadEpsilonDown);
  EXPECT_GE(
      AudioResult::LevelEpsilonDown,
      AudioResult::kPrevLevelEpsilonDown - AudioResult::kPrevDynRangeTolerance);
  EXPECT_LE(
      AudioResult::LevelEpsilonDown,
      AudioResult::kPrevLevelEpsilonDown + AudioResult::kPrevDynRangeTolerance);
  EXPECT_LT(AudioResult::LevelEpsilonDown, unity_level_db);

  EXPECT_GE(AudioResult::SinadEpsilonDown, AudioResult::kPrevSinadEpsilonDown);
}

// Measure dynamic range (signal level, noise floor) when gain is -60dB.
TEST(DynamicRange, 60Down) {
  Gain gain;

  gain.SetRendererGain(-60.0f);
  const Gain::AScale scale = gain.GetGainScale(0.0f);

  MeasureSummaryDynamicRange(scale, &AudioResult::Level60Down,
                             &AudioResult::Sinad60Down);

  EXPECT_GE(AudioResult::Level60Down,
            -60.0 - AudioResult::kPrevDynRangeTolerance);
  EXPECT_LE(AudioResult::Level60Down,
            -60.0 + AudioResult::kPrevDynRangeTolerance);
  EXPECT_GE(AudioResult::Sinad60Down, AudioResult::kPrevSinad60Down);

  // Equivalent gain combo (per-stream, master) should make identical results.
  gain.SetRendererGain(0.0f);
  const Gain::AScale scale2 = gain.GetGainScale(-60.0f);

  double level_db, sinad_db;
  MeasureSummaryDynamicRange(scale2, &level_db, &sinad_db);

  EXPECT_EQ(level_db, AudioResult::Level60Down);
  EXPECT_EQ(sinad_db, AudioResult::Sinad60Down);
}

// Test our mix level and noise floor, when rechannelizing mono into stereo.
TEST(DynamicRange, MonoToStereo) {
  MixerPtr mixer = SelectMixer(AudioSampleFormat::FLOAT, 1, 48000, 2, 48000,
                               Resampler::SampleAndHold);

  std::vector<float> source(kFreqTestBufSize);
  std::vector<int32_t> accum(kFreqTestBufSize * 2);
  std::vector<int32_t> left(kFreqTestBufSize);

  constexpr double amplitude = 1.0;
  constexpr double expected_amplitude =
      amplitude * -std::numeric_limits<int16_t>::min();

  // Populate mono source buffer; mix it (no SRC/gain) to stereo accumulator
  OverwriteCosine(source.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq,
                  amplitude);

  uint32_t dst_offset = 0;
  int32_t frac_src_offset = 0;
  mixer->Mix(accum.data(), kFreqTestBufSize, &dst_offset, source.data(),
             kFreqTestBufSize << kPtsFractionalBits, &frac_src_offset,
             Mixer::FRAC_ONE, Gain::kUnityScale, false);
  EXPECT_EQ(kFreqTestBufSize, dst_offset);
  EXPECT_EQ(static_cast<int32_t>(kFreqTestBufSize << kPtsFractionalBits),
            frac_src_offset);

  // Copy left result to double-float buffer, FFT (freq-analyze) it at high-res
  for (uint32_t idx = 0; idx < kFreqTestBufSize; ++idx) {
    EXPECT_EQ(accum[idx * 2], accum[(idx * 2) + 1]);
    left[idx] = accum[idx * 2];
  }

  // Only need to analyze left side, since we verified that right is identical.
  double magn_left_signal, magn_left_other, level_left_db, sinad_left_db;
  MeasureAudioFreq(left.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq,
                   &magn_left_signal, &magn_left_other);

  level_left_db = ValToDb(magn_left_signal / expected_amplitude);
  sinad_left_db = ValToDb(magn_left_signal / magn_left_other);

  EXPECT_GE(level_left_db, 0 - AudioResult::kLevelToleranceSourceFloat);
  EXPECT_LE(level_left_db, 0 + AudioResult::kLevelToleranceSourceFloat);

  EXPECT_GE(sinad_left_db, AudioResult::kPrevFloorSourceFloat);
}

// Test our mix level and noise floor, when rechannelizing stereo into mono.
TEST(DynamicRange, StereoToMono) {
  MixerPtr mixer = SelectMixer(AudioSampleFormat::FLOAT, 2, 48000, 1, 48000,
                               Resampler::SampleAndHold);

  std::vector<float> mono(kFreqTestBufSize);
  std::vector<float> source(kFreqTestBufSize * 2);
  std::vector<int32_t> accum(kFreqTestBufSize);

  constexpr double amplitude = 1.0;
  constexpr double expected_amplitude =
      amplitude * -std::numeric_limits<int16_t>::min();

  // Populate a mono source buffer; copy it into left side of stereo buffer.
  OverwriteCosine(mono.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq,
                  amplitude);
  for (uint32_t idx = 0; idx < kFreqTestBufSize; ++idx) {
    source[idx * 2] = mono[idx];
  }

  // Populate a mono source buffer; copy it into right side of stereo buffer.
  OverwriteCosine(mono.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq,
                  amplitude, M_PI / 2);
  for (uint32_t idx = 0; idx < kFreqTestBufSize; ++idx) {
    source[(idx * 2) + 1] = mono[idx];
  }

  uint32_t dst_offset = 0;
  int32_t frac_src_offset = 0;
  mixer->Mix(accum.data(), kFreqTestBufSize, &dst_offset, source.data(),
             kFreqTestBufSize << kPtsFractionalBits, &frac_src_offset,
             Mixer::FRAC_ONE, Gain::kUnityScale, false);
  EXPECT_EQ(kFreqTestBufSize, dst_offset);
  EXPECT_EQ(static_cast<int32_t>(kFreqTestBufSize << kPtsFractionalBits),
            frac_src_offset);

  // Copy result to double-float buffer, FFT (freq-analyze) it at high-res
  double magn_signal, magn_other, level_mono_db;
  MeasureAudioFreq(accum.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq,
                   &magn_signal, &magn_other);

  level_mono_db = ValToDb(magn_signal / expected_amplitude);
  AudioResult::FloorStereoMono =
      ValToDb(std::numeric_limits<int16_t>::max() / magn_other);

  // We added identical signals, so accuracy should be high. However, noise
  // floor is doubled as well, so we expect 6dB reduction in sinad.
  EXPECT_GE(level_mono_db, AudioResult::kPrevLevelStereoMono -
                               AudioResult::kPrevStereoMonoTolerance);
  EXPECT_LE(level_mono_db, AudioResult::kPrevLevelStereoMono +
                               AudioResult::kPrevStereoMonoTolerance);

  EXPECT_GE(AudioResult::FloorStereoMono, AudioResult::kPrevFloorStereoMono);
}

// Test mix level and noise floor, when accumulating sources.
//
// Mix 2 full-scale streams with gain exactly 50% (renderer 100%, master 50%),
// then measure level and sinad. On systems with robust gain processing, a
// post-SUM master gain stage reduces noise along with level, for the same noise
// floor as a single FS signal with 100% gain (98,49 dB for 16,8 respectively).
//
// When summing two full-scale streams, signal should be approx +6dBFS, and
// noise floor should be related to the bitwidth of source and accumulator
// (whichever is more narrow). Because our accumulator is still normalized to
// 16 bits, we expect the single-stream noise floor to be approx. 98 dB. This
// test emulates the mixing of two streams, along with the application of a
// master gain which reduces the mixed result to 50%, which should result in a
// signal which is exactly full-scale. Summing the two streams will sum the
// inherent noise as well, leading to a noise floor of 91-92 dB before taking
// gain into account. Once our architecture contains a post-SUM master gain,
// after applying a 0.5 master gain scaling we would expect this 91-92 dB
// SINAD to be reduced to perhaps 98 dB. Today master gain is combined with
// renderer gain, so it is pre-Sum.
template <typename T>
void MeasureMixFloor(double* level_mix_db, double* sinad_mix_db) {
  MixerPtr mixer;
  double amplitude, expected_amplitude;

  if (std::is_same<T, uint8_t>::value) {
    mixer = SelectMixer(AudioSampleFormat::UNSIGNED_8, 1, 48000, 1, 48000,
                        Resampler::SampleAndHold);
    amplitude = std::numeric_limits<int8_t>::max();
    expected_amplitude = amplitude * (1 << 8);
  } else if (std::is_same<T, float>::value) {
    mixer = SelectMixer(AudioSampleFormat::FLOAT, 1, 48000, 1, 48000,
                        Resampler::SampleAndHold);
    amplitude = 1.0;
    expected_amplitude = amplitude * -std::numeric_limits<int16_t>::min();
  } else {
    mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                        Resampler::SampleAndHold);
    amplitude = std::numeric_limits<int16_t>::max();
    expected_amplitude = std::numeric_limits<int16_t>::max();
  }
  std::vector<T> source(kFreqTestBufSize);
  std::vector<int32_t> accum(kFreqTestBufSize);

  OverwriteCosine(source.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq,
                  amplitude);

  uint32_t dst_offset = 0;
  int32_t frac_src_offset = 0;
  EXPECT_TRUE(mixer->Mix(accum.data(), kFreqTestBufSize, &dst_offset,
                         source.data(), kFreqTestBufSize << kPtsFractionalBits,
                         &frac_src_offset, Mixer::FRAC_ONE,
                         Gain::kUnityScale >> 1, false));

  // Accumulate the same (reference-frequency) wave.
  dst_offset = 0;
  frac_src_offset = 0;
  EXPECT_TRUE(mixer->Mix(accum.data(), kFreqTestBufSize, &dst_offset,
                         source.data(), kFreqTestBufSize << kPtsFractionalBits,
                         &frac_src_offset, Mixer::FRAC_ONE,
                         Gain::kUnityScale >> 1, true));
  EXPECT_EQ(kFreqTestBufSize, dst_offset);
  EXPECT_EQ(dst_offset << kPtsFractionalBits,
            static_cast<uint32_t>(frac_src_offset));

  // Copy result to double-float buffer, FFT (freq-analyze) it at high-res
  double magn_signal, magn_other;
  MeasureAudioFreq(accum.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq,
                   &magn_signal, &magn_other);

  *level_mix_db = ValToDb(magn_signal / expected_amplitude);
  *sinad_mix_db = ValToDb(std::numeric_limits<int16_t>::max() / magn_other);
}

// Test our mix level and noise floor, when accumulating 8-bit sources.
TEST(DynamicRange, Mix_8) {
  MeasureMixFloor<uint8_t>(&AudioResult::LevelMix8, &AudioResult::FloorMix8);

  EXPECT_GE(AudioResult::LevelMix8, 0.0 - AudioResult::kLevelToleranceMix8);
  EXPECT_LE(AudioResult::LevelMix8, 0.0 + AudioResult::kLevelToleranceMix8);

  // 8-bit noise floor should be approx -48dBFS. Because 8-bit sources are
  // normalized up to 16-bit level, they can take advantage of fractional
  // "footroom"; hence we still expect sinad of ~48dB.
  EXPECT_GE(AudioResult::FloorMix8, AudioResult::kPrevFloorMix8);
}

// Test our mix level and noise floor, when accumulating 16-bit sources.
TEST(DynamicRange, Mix_16) {
  MeasureMixFloor<int16_t>(&AudioResult::LevelMix16, &AudioResult::FloorMix16);

  EXPECT_GE(AudioResult::LevelMix16, 0.0 - AudioResult::kLevelToleranceMix16);
  EXPECT_LE(AudioResult::LevelMix16, 0.0 + AudioResult::kLevelToleranceMix16);

  // 16-bit noise floor should be approx -96dBFS. Noise is summed along with
  // signal; therefore we expect sinad of ~90 dB.
  EXPECT_GE(AudioResult::FloorMix16, AudioResult::kPrevFloorMix16);
}

// Test our mix level and noise floor, when accumulating float sources.
TEST(DynamicRange, Mix_Float) {
  MeasureMixFloor<float>(&AudioResult::LevelMixFloat,
                         &AudioResult::FloorMixFloat);

  EXPECT_GE(AudioResult::LevelMixFloat,
            0.0 - AudioResult::kLevelToleranceMixFloat);
  EXPECT_LE(AudioResult::LevelMixFloat,
            0.0 + AudioResult::kLevelToleranceMixFloat);

  // This should be same as 16-bit (~91dB), per accumulator precision. Once we
  // increase accumulator precision, we expect this to improve, while Mix_16
  // would not, as precision will still be limited by its 16-bit source.
  EXPECT_GE(AudioResult::FloorMixFloat, AudioResult::kPrevFloorMixFloat);
}

}  // namespace test
}  // namespace audio
}  // namespace media
