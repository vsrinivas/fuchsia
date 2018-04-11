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
  MixerPtr mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                               Resampler::SampleAndHold);
  constexpr double amplitude = std::numeric_limits<int16_t>::max();

  std::vector<int16_t> source(kFreqTestBufSize);
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

  *level_db = ValToDb(magn_signal / amplitude);
  *sinad_db = ValToDb(magn_signal / magn_other);
}

// Measure dynamic range at two gain settings: less than 1.0 by the smallest
// increment possible, as well as the smallest increment detectable (the
// closest-to-1.0 gain that actually causes incoming data values to change).
TEST(DynamicRange, Epsilon) {
  double unity_level_db, unity_sinad_db, level_db, sinad_db;

  MeasureSummaryDynamicRange(Gain::kUnityScale, &unity_level_db,
                             &unity_sinad_db);
  EXPECT_GE(unity_level_db, -AudioResult::kLevelToleranceSource16);
  EXPECT_LE(unity_level_db, AudioResult::kLevelToleranceSource16);
  EXPECT_GE(unity_sinad_db, AudioResult::kPrevFloorSource16);

  // At this gain_scale, we should not observe an effect different than unity.
  static_assert(AudioResult::kScaleEpsilon < Gain::kUnityScale - 1,
                "kScaleEpsilon should be less than kUnityScale - 1");
  MeasureSummaryDynamicRange(Gain::kUnityScale - 1, &level_db, &sinad_db);
  EXPECT_EQ(level_db, unity_level_db);
  EXPECT_EQ(sinad_db, unity_sinad_db);

  // kScaleEpsilon: nearest-unity scale at which we observe effects on signals.
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

  // Validate level & floor in equivalent gain combination (per-stream, master).
  gain.SetRendererGain(0.0f);
  const Gain::AScale scale2 = gain.GetGainScale(-60.0f);

  double level_db, sinad_db;
  MeasureSummaryDynamicRange(scale2, &level_db, &sinad_db);

  EXPECT_EQ(level_db, AudioResult::Level60Down);
  EXPECT_EQ(sinad_db, AudioResult::Sinad60Down);
}

// Test our mix level and noise floor, when rechannelizing mono into stereo.
TEST(DynamicRange, MonoToStereo) {
  MixerPtr mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 2, 48000,
                               Resampler::SampleAndHold);

  std::vector<int16_t> source(kFreqTestBufSize);
  std::vector<int32_t> accum(kFreqTestBufSize * 2);
  std::vector<int32_t> left(kFreqTestBufSize);

  // Populate mono source buffer; mix it (no SRC/gain) to stereo accumulator
  OverwriteCosine(source.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq,
                  std::numeric_limits<int16_t>::max());

  uint32_t dst_offset = 0;
  int32_t frac_src_offset = 0;
  mixer->Mix(accum.data(), kFreqTestBufSize, &dst_offset, source.data(),
             kFreqTestBufSize << kPtsFractionalBits, &frac_src_offset,
             Mixer::FRAC_ONE, Gain::kUnityScale, false);
  EXPECT_EQ(kFreqTestBufSize, dst_offset);
  EXPECT_EQ(static_cast<int32_t>(kFreqTestBufSize << kPtsFractionalBits),
            frac_src_offset);

  // Copy left result to double-float buffer, FFT (freq-analyze) it at high-res
  // Only need to analyze left side, since we verified that right is identical.
  for (uint32_t idx = 0; idx < kFreqTestBufSize; ++idx) {
    EXPECT_EQ(accum[idx * 2], accum[(idx * 2) + 1]);
    left[idx] = accum[idx * 2];
  }

  double magn_left_signal, magn_left_other, level_left_db, sinad_left_db;
  MeasureAudioFreq(left.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq,
                   &magn_left_signal, &magn_left_other);

  level_left_db =
      ValToDb(magn_left_signal / std::numeric_limits<int16_t>::max());
  sinad_left_db = ValToDb(magn_left_signal / magn_left_other);

  EXPECT_GE(level_left_db, 0 - AudioResult::kLevelToleranceSource16);
  EXPECT_LE(level_left_db, 0 + AudioResult::kLevelToleranceSource16);

  EXPECT_GE(sinad_left_db, AudioResult::kPrevFloorSource16);
}

// Test our mix level and noise floor, when rechannelizing stereo into mono.
TEST(DynamicRange, StereoToMono) {
  MixerPtr mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 2, 48000, 1, 48000,
                               Resampler::SampleAndHold);

  std::vector<int16_t> mono(kFreqTestBufSize);
  std::vector<int16_t> source(kFreqTestBufSize * 2);
  std::vector<int32_t> accum(kFreqTestBufSize);

  // Populate mono source buffer; copy it into stereo source buffer
  OverwriteCosine(mono.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq,
                  std::numeric_limits<int16_t>::max());
  for (uint32_t idx = 0; idx < kFreqTestBufSize; ++idx) {
    source[idx * 2] = mono[idx];
  }

  // Populate mono source buffer; copy it into stereo source buffer
  OverwriteCosine(mono.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq,
                  std::numeric_limits<int16_t>::max(), M_PI / 2);
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

  level_mono_db = ValToDb(magn_signal / std::numeric_limits<int16_t>::max());
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
// Mix 2 full-scale streams with gain exactly 50% (renderer 100%, master 50%),
// then measure level and sinad. On systems with robust gain processing, a
// post-SUM master gain stage reduces noise along with level, for the same noise
// floor as a single FS signal with 100% gain (98,49 dB for 16,8 respectively).
template <typename T>
void MeasureMixFloor(double* level_mix_db, double* sinad_mix_db) {
  MixerPtr mixer;
  double amplitude;

  if (std::is_same<T, uint8_t>::value) {
    mixer = SelectMixer(AudioSampleFormat::UNSIGNED_8, 1, 48000, 1, 48000,
                        Resampler::SampleAndHold);
    amplitude = std::numeric_limits<int8_t>::max();
  } else if (std::is_same<T, float>::value) {
    mixer = SelectMixer(AudioSampleFormat::FLOAT, 1, 48000, 1, 48000,
                        Resampler::SampleAndHold);
    amplitude = static_cast<double>(-std::numeric_limits<int16_t>::max()) /
                std::numeric_limits<int16_t>::min();
  } else {
    mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                        Resampler::SampleAndHold);
    amplitude = std::numeric_limits<int16_t>::max();
  }
  std::vector<T> source(kFreqTestBufSize);
  std::vector<int32_t> accum(kFreqTestBufSize);

  OverwriteCosine(source.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq,
                  amplitude);
  uint32_t dst_offset = 0;
  int32_t frac_src_offset = 0;
  mixer->Mix(accum.data(), kFreqTestBufSize, &dst_offset, source.data(),
             kFreqTestBufSize << kPtsFractionalBits, &frac_src_offset,
             Mixer::FRAC_ONE, Gain::kUnityScale >> 1, false);
  EXPECT_EQ(kFreqTestBufSize, dst_offset);
  EXPECT_EQ(static_cast<int32_t>(kFreqTestBufSize << kPtsFractionalBits),
            frac_src_offset);

  // Accumulate the same (reference-frequency) wave
  dst_offset = 0;
  frac_src_offset = 0;
  mixer->Mix(accum.data(), kFreqTestBufSize, &dst_offset, source.data(),
             kFreqTestBufSize << kPtsFractionalBits, &frac_src_offset,
             Mixer::FRAC_ONE, Gain::kUnityScale >> 1, true);
  EXPECT_EQ(kFreqTestBufSize, dst_offset);
  EXPECT_EQ(static_cast<int32_t>(kFreqTestBufSize << kPtsFractionalBits),
            frac_src_offset);

  // Copy result to double-float buffer, FFT (freq-analyze) it at high-res
  double magn_signal, magn_other;
  MeasureAudioFreq(accum.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq,
                   &magn_signal, &magn_other);

  *level_mix_db = ValToDb(magn_signal / std::numeric_limits<int16_t>::max());
  *sinad_mix_db = ValToDb(magn_signal / magn_other);
}

// Test our mix level and noise floor, when accumulating 8-bit sources.
TEST(DynamicRange, Mix_8) {
  MeasureMixFloor<uint8_t>(&AudioResult::LevelMix8, &AudioResult::FloorMix8);

  EXPECT_GE(AudioResult::LevelMix8, -AudioResult::kLevelToleranceMix8);
  EXPECT_LE(AudioResult::LevelMix8, AudioResult::kLevelToleranceMix8);

  // When summing two full-scale streams, signal should be approx +6dBFS, and
  // (8-bit) noise floor should be approx -43dBFS. If architecture contains
  // post-SUM master gain, after 50% gain we would expect sinad of ~ 49 dB.
  // Today master gain is combined with renderer gain, making it pre-Sum.
  // Because 8-bit sources are normalized up to 16-bit level, they can take
  // advantage of fractional "footroom"; hence we still expect sinad of ~ 49dB.
  EXPECT_GE(AudioResult::FloorMix8, AudioResult::kPrevFloorMix8);
}

// Test our mix level and noise floor, when accumulating 16-bit sources.
TEST(DynamicRange, Mix_16) {
  MeasureMixFloor<int16_t>(&AudioResult::LevelMix16, &AudioResult::FloorMix16);

  EXPECT_GE(AudioResult::LevelMix16, -AudioResult::kLevelToleranceMix16);
  EXPECT_LE(AudioResult::LevelMix16, AudioResult::kLevelToleranceMix16);

  // When summing two full-scale streams, signal should be approx +6dBFS, and
  // (16-bit) noise floor should be approx -92dBFS. If architecture contains
  // post-SUM master gain, after 50% gain we would expect sinad of ~ 98 dB.
  // Today master gain is combined with renderer gain, making it pre-Sum. Noise
  // is summed along with signal; therefore we expect sinad of ~ 90dB.
  EXPECT_GE(AudioResult::FloorMix16, AudioResult::kPrevFloorMix16);
}

// Test our mix level and noise floor, when accumulating float sources.
TEST(DynamicRange, Mix_Float) {
  MeasureMixFloor<float>(&AudioResult::LevelMixFloat,
                         &AudioResult::FloorMixFloat);

  EXPECT_GE(AudioResult::LevelMixFloat, -AudioResult::kLevelToleranceMixFloat);
  EXPECT_LE(AudioResult::LevelMixFloat, AudioResult::kLevelToleranceMixFloat);

  // When summing two full-scale streams, signal should be approx +6dBFS, and
  // noise floor should be approx -92dBFS. If architecture contains post-SUM
  // master gain, after 50% gain we would expect sinad of ~ 98 dB. Today master
  // gain is combined with renderer gain, making it pre-Sum. Noise is summed
  // along with signal; therefore we expect sinad of ~ 90dB.
  EXPECT_GE(AudioResult::FloorMixFloat, AudioResult::kPrevFloorMixFloat);
}

}  // namespace test
}  // namespace audio
}  // namespace media
