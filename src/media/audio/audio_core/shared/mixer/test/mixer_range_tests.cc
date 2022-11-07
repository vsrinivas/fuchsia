// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/shared/mixer/test/audio_result.h"
#include "src/media/audio/audio_core/shared/mixer/test/mixer_tests_shared.h"
#include "src/media/audio/lib/analysis/generators.h"
#include "src/media/audio/lib/format/audio_buffer.h"
#include "src/media/audio/lib/processing/gain.h"

namespace media::audio::test {

// Convenience abbreviations within this source file to shorten names.
using Resampler = ::media::audio::Mixer::Resampler;
using ASF = fuchsia::media::AudioSampleFormat;

// Ideal dynamic range measurement is exactly equal to the reduction in gain.
// Ideal accompanying noise is ideal noise floor, minus the reduction in gain.
void MeasureSummaryDynamicRange(float gain_db, double* level_db, double* sinad_db) {
  auto mixer = SelectMixer(ASF::FLOAT, 1, 48000, 1, 48000, Resampler::SampleAndHold);
  ASSERT_NE(mixer, nullptr);

  auto format = Format::Create<ASF::FLOAT>(1, 48000).take_value();

  // Populate source buffer; mix it (pass-thru) to accumulation buffer
  auto source = GenerateCosineAudio(format, kFreqTestBufSize, FrequencySet::kReferenceFreq);
  AudioBuffer accum(format, kFreqTestBufSize);

  int64_t dest_offset = 0;
  auto source_offset = Fixed(0);

  mixer->gain.SetSourceGain(gain_db);
  mixer->Mix(&accum.samples()[0], kFreqTestBufSize, &dest_offset, &source.samples()[0],
             kFreqTestBufSize, &source_offset, false);
  EXPECT_EQ(dest_offset, kFreqTestBufSize);
  EXPECT_EQ(source_offset, Fixed(kFreqTestBufSize));

  // Copy result to double-float buffer, FFT (freq-analyze) it at high-res
  auto result = MeasureAudioFreq(AudioBufferSlice(&accum), FrequencySet::kReferenceFreq);
  *level_db = DoubleToDb(result.total_magn_signal);
  *sinad_db = DoubleToDb(result.total_magn_signal / result.total_magn_other);
}

// Measure dynamic range at two gain settings: less than 1.0 by the smallest
// increment possible, as well as the smallest increment detectable (the
// closest-to-1.0 gain that actually causes incoming data values to change).
TEST(DynamicRange, Epsilon) {
  double unity_level_db, unity_sinad_db;
  MeasureSummaryDynamicRange(0.0f, &unity_level_db, &unity_sinad_db);
  EXPECT_NEAR(unity_level_db, 0.0, AudioResult::kPrevLevelToleranceSourceFloat);
  EXPECT_GE(unity_sinad_db, AudioResult::kPrevFloorSourceFloat);
  AudioResult::LevelToleranceSourceFloat =
      fmax(AudioResult::LevelToleranceSourceFloat, abs(unity_level_db));

  // kMinGainDbUnity is the lowest (furthest-from-Unity) with no observable
  // attenuation on float32 (i.e. the smallest indistinguishable from Unity).
  // Just above the 'first detectable reduction' scale; should be same as unity.
  double near_unity_level_db, near_unity_sinad_db;
  MeasureSummaryDynamicRange(AudioResult::kMinGainDbUnity, &near_unity_level_db,
                             &near_unity_sinad_db);
  EXPECT_DOUBLE_EQ(near_unity_level_db, unity_level_db);
  EXPECT_DOUBLE_EQ(near_unity_sinad_db, unity_sinad_db);

  // kMaxGainDbNonUnity is the highest (closest-to-Unity) with observable effect
  // on full-scale (i.e. largest sub-Unity AScale distinguishable from Unity).
  // At this 'detectable reduction' scale, level and noise floor are reduced.
  MeasureSummaryDynamicRange(AudioResult::kMaxGainDbNonUnity, &AudioResult::LevelEpsilonDown,
                             &AudioResult::SinadEpsilonDown);
  EXPECT_NEAR(AudioResult::LevelEpsilonDown, AudioResult::kPrevLevelEpsilonDown,
              AudioResult::kPrevDynRangeTolerance);
  AudioResult::DynRangeTolerance =
      fmax(AudioResult::DynRangeTolerance,
           abs(AudioResult::LevelEpsilonDown - AudioResult::kPrevLevelEpsilonDown));

  EXPECT_LT(AudioResult::LevelEpsilonDown, unity_level_db);
  EXPECT_GE(AudioResult::SinadEpsilonDown, AudioResult::kPrevSinadEpsilonDown);

  // Update the min distinguishable gain value, for display later (if --dump).
  if (near_unity_level_db < unity_level_db) {
    AudioResult::ScaleEpsilon = AudioResult::kMinGainDbUnity;
  } else if (AudioResult::LevelEpsilonDown < unity_level_db) {
    AudioResult::ScaleEpsilon = AudioResult::kMaxGainDbNonUnity;
  }
}

// Measure dynamic range (signal level, noise floor) when gain is -30dB.
TEST(DynamicRange, 30Down) {
  MeasureSummaryDynamicRange(-30.0f, &AudioResult::Level30Down, &AudioResult::Sinad30Down);
  AudioResult::DynRangeTolerance =
      fmax(AudioResult::DynRangeTolerance, abs(AudioResult::Level30Down + 30.0));

  EXPECT_NEAR(AudioResult::Level30Down, -30.0, AudioResult::kPrevDynRangeTolerance);
  EXPECT_GE(AudioResult::Sinad30Down, AudioResult::kPrevSinad30Down);
}

// Measure dynamic range (signal level, noise floor) when gain is -60dB.
TEST(DynamicRange, 60Down) {
  MeasureSummaryDynamicRange(-60.0f, &AudioResult::Level60Down, &AudioResult::Sinad60Down);
  AudioResult::DynRangeTolerance =
      fmax(AudioResult::DynRangeTolerance, abs(AudioResult::Level60Down + 60.0));

  EXPECT_NEAR(AudioResult::Level60Down, -60.0, AudioResult::kPrevDynRangeTolerance);
  EXPECT_GE(AudioResult::Sinad60Down, AudioResult::kPrevSinad60Down);
}

// Measure dynamic range (signal level, noise floor) when gain is -90dB.
TEST(DynamicRange, 90Down) {
  MeasureSummaryDynamicRange(-90.0f, &AudioResult::Level90Down, &AudioResult::Sinad90Down);
  AudioResult::DynRangeTolerance =
      fmax(AudioResult::DynRangeTolerance, abs(AudioResult::Level90Down + 90.0));

  EXPECT_NEAR(AudioResult::Level90Down, -90.0, AudioResult::kPrevDynRangeTolerance);
  EXPECT_GE(AudioResult::Sinad90Down, AudioResult::kPrevSinad90Down);
}

// Test our mix level and noise floor, when rechannelizing mono into stereo.
TEST(DynamicRange, MonoToStereo) {
  auto mixer = SelectMixer(ASF::FLOAT, 1, 48000, 2, 48000, Resampler::SampleAndHold);
  ASSERT_NE(mixer, nullptr);

  auto mono_format = Format::Create<ASF::FLOAT>(1, 48000).take_value();
  auto stereo_format = Format::Create<ASF::FLOAT>(2, 48000).take_value();

  // Populate mono source buffer; mix it (no SRC/gain) to stereo accumulator
  auto source = GenerateCosineAudio(mono_format, kFreqTestBufSize, FrequencySet::kReferenceFreq);

  AudioBuffer accum(stereo_format, kFreqTestBufSize);
  AudioBuffer left(mono_format, kFreqTestBufSize);

  int64_t dest_offset = 0;
  auto source_offset = Fixed(0);

  mixer->Mix(&accum.samples()[0], kFreqTestBufSize, &dest_offset, &source.samples()[0],
             kFreqTestBufSize, &source_offset, false);
  EXPECT_EQ(dest_offset, kFreqTestBufSize);
  EXPECT_EQ(source_offset, Fixed(kFreqTestBufSize));

  // Copy left result to double-float buffer, FFT (freq-analyze) it at high-res
  for (auto idx = 0; idx < kFreqTestBufSize; ++idx) {
    EXPECT_FLOAT_EQ(accum.samples()[idx * 2], accum.samples()[(idx * 2) + 1]);
    left.samples()[idx] = accum.samples()[idx * 2];
  }

  // Only need to analyze left side, since we verified that right is identical.
  auto left_result = MeasureAudioFreq(AudioBufferSlice(&left), FrequencySet::kReferenceFreq);
  auto level_left_db = DoubleToDb(left_result.total_magn_signal);
  auto sinad_left_db = DoubleToDb(left_result.total_magn_signal / left_result.total_magn_other);

  EXPECT_NEAR(level_left_db, 0.0, AudioResult::kPrevLevelToleranceSourceFloat);
  AudioResult::LevelToleranceSourceFloat =
      fmax(AudioResult::LevelToleranceSourceFloat, abs(level_left_db));

  EXPECT_GE(sinad_left_db, AudioResult::kPrevFloorSourceFloat);
}

// Test our mix level and noise floor, when rechannelizing stereo into mono.
TEST(DynamicRange, StereoToMono) {
  auto mixer = SelectMixer(ASF::FLOAT, 2, 48000, 1, 48000, Resampler::SampleAndHold);
  ASSERT_NE(mixer, nullptr);

  auto mono_format = Format::Create<ASF::FLOAT>(1, 48000).take_value();
  auto stereo_format = Format::Create<ASF::FLOAT>(2, 48000).take_value();

  // Populate a mono source buffer; copy it into left side of stereo buffer.
  auto mono = GenerateCosineAudio(mono_format, kFreqTestBufSize, FrequencySet::kReferenceFreq,
                                  kFullScaleFloatInputAmplitude, 0.0);
  AudioBuffer source(stereo_format, kFreqTestBufSize);
  AudioBuffer accum(mono_format, kFreqTestBufSize);

  for (auto idx = 0; idx < kFreqTestBufSize; ++idx) {
    source.samples()[idx * 2] = mono.samples()[idx];
  }

  // Populate a mono source buffer with same frequency and amplitude, phase-
  // shifted by PI/2 (1/4 of a cycle); copy it into right side of stereo buffer.
  mono = GenerateCosineAudio(mono_format, kFreqTestBufSize, FrequencySet::kReferenceFreq,
                             kFullScaleFloatInputAmplitude, M_PI / 2);
  for (auto idx = 0; idx < kFreqTestBufSize; ++idx) {
    source.samples()[(idx * 2) + 1] = mono.samples()[idx];
  }

  int64_t dest_offset = 0;
  auto source_offset = Fixed(0);

  mixer->Mix(&accum.samples()[0], kFreqTestBufSize, &dest_offset, &source.samples()[0],
             kFreqTestBufSize, &source_offset, false);
  EXPECT_EQ(dest_offset, kFreqTestBufSize);
  EXPECT_EQ(source_offset, Fixed(kFreqTestBufSize));

  // Copy result to double-float buffer, FFT (freq-analyze) it at high-res
  auto result = MeasureAudioFreq(AudioBufferSlice(&accum), FrequencySet::kReferenceFreq);

  AudioResult::LevelStereoMono = DoubleToDb(result.total_magn_signal);
  AudioResult::FloorStereoMono =
      DoubleToDb(kFullScaleFloatAccumAmplitude / result.total_magn_other);

  // We added identical signals, so accuracy should be high. However, noise
  // floor is doubled as well, so we expect 6dB reduction in sinad.
  EXPECT_NEAR(AudioResult::LevelStereoMono, AudioResult::kPrevLevelStereoMono,
              AudioResult::kPrevLevelToleranceStereoMono);
  AudioResult::LevelToleranceStereoMono =
      fmax(AudioResult::LevelToleranceStereoMono,
           abs(AudioResult::LevelStereoMono - AudioResult::kPrevLevelStereoMono));

  EXPECT_GE(AudioResult::FloorStereoMono, AudioResult::kPrevFloorStereoMono);
}

// Test mix level and noise floor, when accumulating sources.
//
// Mix 2 full-scale streams with gain exactly 50% (source gain 100%, sink gain 50%),
// then measure level and sinad. On systems with robust gain processing, a post-SUM
// group "submix" or final gain stage reduces noise along with level, for the same
// noise floor as a single FS signal with 100% gain (98,49 dB for 16,8 respectively).
//
// When summing two full-scale streams, signal should be approx +6 dBFS, and noise
// floor should be related to the bitwidth of source and accumulator (whichever is
// more narrow). Note: summing 2 streams also doubles the inherent noise. For 8-bit
// and 16-bit sources (with float32 accumuator), the source limits the noise floor.
template <ASF SampleFormat>
void MeasureMixFloor(double* level_mix_db, double* sinad_mix_db) {
  auto mixer = SelectMixer(SampleFormat, 1, 48000, 1, 48000, Resampler::SampleAndHold);
  ASSERT_NE(mixer, nullptr);

  auto [amplitude, expected_amplitude] = SampleFormatToAmplitudes(SampleFormat);
  auto format = Format::Create<SampleFormat>(1, 48000).take_value();
  auto float_format = Format::Create<ASF::FLOAT>(1, 48000).take_value();

  auto source =
      GenerateCosineAudio(format, kFreqTestBufSize, FrequencySet::kReferenceFreq, amplitude);
  AudioBuffer accum(float_format, kFreqTestBufSize);

  int64_t dest_offset = 0;
  auto source_offset = Fixed(0);

  // -6.0206 dB leads to 0.500 scale (exactly 50%), to be mixed with itself
  mixer->gain.SetSourceGain(-6.0205999f);
  mixer->Mix(&accum.samples()[0], kFreqTestBufSize, &dest_offset, &source.samples()[0],
             kFreqTestBufSize, &source_offset, false);
  EXPECT_EQ(source_offset, Fixed(kFreqTestBufSize));

  // Accumulate the same (reference-frequency) wave.
  dest_offset = 0;
  source_offset = Fixed(0);

  mixer->Mix(&accum.samples()[0], kFreqTestBufSize, &dest_offset, &source.samples()[0],
             kFreqTestBufSize, &source_offset, true);
  EXPECT_EQ(dest_offset, kFreqTestBufSize);
  EXPECT_EQ(source_offset, Fixed(dest_offset));

  // Copy result to double-float buffer, FFT (freq-analyze) it at high-res
  auto result = MeasureAudioFreq(AudioBufferSlice(&accum), FrequencySet::kReferenceFreq);

  *level_mix_db = DoubleToDb(result.total_magn_signal / expected_amplitude);
  *sinad_mix_db = DoubleToDb(expected_amplitude / result.total_magn_other);
}

// Test our mix level and noise floor, when accumulating 8-bit sources.
TEST(DynamicRange, MixUint8) {
  MeasureMixFloor<ASF::UNSIGNED_8>(&AudioResult::LevelMix8, &AudioResult::FloorMix8);

  EXPECT_NEAR(AudioResult::LevelMix8, 0.0, AudioResult::kPrevLevelToleranceMix8);
  AudioResult::LevelToleranceMix8 =
      fmax(AudioResult::LevelToleranceMix8, abs(AudioResult::LevelMix8));

  // 8-bit noise floor should be approx -48dBFS. Because 8-bit sources are
  // normalized up to 16-bit level, they can take advantage of fractional
  // "footroom"; hence we still expect sinad of ~48dB.
  EXPECT_GE(AudioResult::FloorMix8, AudioResult::kPrevFloorMix8)
      << std::setprecision(10) << AudioResult::FloorMix8;
}

// Test our mix level and noise floor, when accumulating 16-bit sources.
TEST(DynamicRange, MixInt16) {
  MeasureMixFloor<ASF::SIGNED_16>(&AudioResult::LevelMix16, &AudioResult::FloorMix16);

  EXPECT_NEAR(AudioResult::LevelMix16, 0.0, AudioResult::kPrevLevelToleranceMix16);
  AudioResult::LevelToleranceMix16 =
      fmax(AudioResult::LevelToleranceMix16, abs(AudioResult::LevelMix16));

  // 16-bit noise floor should be approx -96dBFS. Noise is summed along with
  // signal; therefore we expect sinad of ~90 dB.
  EXPECT_GE(AudioResult::FloorMix16, AudioResult::kPrevFloorMix16)
      << std::setprecision(10) << AudioResult::FloorMix16;
}

// Test our mix level and noise floor, when accumulating 24-bit sources.
TEST(DynamicRange, MixInt24) {
  MeasureMixFloor<ASF::SIGNED_24_IN_32>(&AudioResult::LevelMix24, &AudioResult::FloorMix24);

  EXPECT_NEAR(AudioResult::LevelMix24, 0.0, AudioResult::kPrevLevelToleranceMix24);
  AudioResult::LevelToleranceMix24 =
      fmax(AudioResult::LevelToleranceMix24, abs(AudioResult::LevelMix24));

  // 24-bit noise floor should be approx -144dBFS. Noise is summed along with
  // signal; therefore we expect sinad of ~138 dB.
  EXPECT_GE(AudioResult::FloorMix24, AudioResult::kPrevFloorMix24)
      << std::setprecision(10) << AudioResult::FloorMix24;
}

// Test our mix level and noise floor, when accumulating float sources.
TEST(DynamicRange, MixFloat32) {
  MeasureMixFloor<ASF::FLOAT>(&AudioResult::LevelMixFloat, &AudioResult::FloorMixFloat);

  EXPECT_NEAR(AudioResult::LevelMixFloat, 0.0, AudioResult::kPrevLevelToleranceMixFloat);
  AudioResult::LevelToleranceMixFloat =
      fmax(AudioResult::LevelToleranceMixFloat, abs(AudioResult::LevelMixFloat));

  // This should be same as 16-bit (~91dB), per accumulator precision. Once we
  // increase accumulator precision, we expect this to improve, while Mix_16
  // would not, as precision will still be limited by its 16-bit source.
  EXPECT_GE(AudioResult::FloorMixFloat, AudioResult::kPrevFloorMixFloat)
      << std::setprecision(10) << AudioResult::FloorMixFloat;
}

}  // namespace media::audio::test
