// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/mixer/test/audio_result.h"
#include "garnet/bin/media/audio_server/mixer/test/mixer_tests_shared.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {
namespace test {

// Convenience abbreviation within this source file to shorten names
using Resampler = media::audio::Mixer::Resampler;

//
// Baseline Noise-Floor tests
//
// These tests determine our best-case audio quality/fidelity, in the absence of
// any gain, interpolation/SRC, mixing, reformatting or other processing. These
// tests are done with a single 1kHz tone, and provide a baseline from which we
// can measure any changes in sonic quality caused by other mixer stages.
//
// In performing all of our audio analysis tests with a specific buffer length,
// We can choose input sinusoids with frequencies that perfectly fit within
// those buffers (eliminating the need for FFT windowing). The reference
// frequency below was specifically designed as an approximation of a 1kHz tone,
// assuming an eventual 48kHz output sample rate.
template <typename T>
double MeasureSourceNoiseFloor(double* sinad_db) {
  MixerPtr mixer;
  double amplitude, expected_amplitude;

  if (std::is_same<T, uint8_t>::value) {
    mixer = SelectMixer(fuchsia::media::AudioSampleFormat::UNSIGNED_8, 1, 48000,
                        1, 48000, Resampler::SampleAndHold);
    amplitude = std::numeric_limits<int8_t>::max();
    expected_amplitude = amplitude * (1 << (kAudioPipelineWidth - 8));
  } else if (std::is_same<T, int16_t>::value) {
    mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000,
                        1, 48000, Resampler::SampleAndHold);
    amplitude = std::numeric_limits<int16_t>::max();
    expected_amplitude = amplitude * (1 << (kAudioPipelineWidth - 16));
  } else if (std::is_same<T, float>::value) {
    mixer = SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, 48000, 1,
                        48000, Resampler::SampleAndHold);
    amplitude = 1.0;
    expected_amplitude = amplitude * (1 << (kAudioPipelineWidth - 1));
  } else {
    FXL_DCHECK(false) << "Unsupported source format";
  }

  // Populate source buffer; mix it (pass-thru) to accumulation buffer
  std::vector<T> source(kFreqTestBufSize);
  OverwriteCosine(source.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq,
                  amplitude);

  std::vector<int32_t> accum(kFreqTestBufSize);
  uint32_t dst_offset = 0;
  int32_t frac_src_offset = 0;
  mixer->Mix(accum.data(), kFreqTestBufSize, &dst_offset, source.data(),
             kFreqTestBufSize << kPtsFractionalBits, &frac_src_offset,
             Mixer::FRAC_ONE, Gain::kUnityScale, false);
  EXPECT_EQ(kFreqTestBufSize, dst_offset);
  EXPECT_EQ(static_cast<int32_t>(kFreqTestBufSize << kPtsFractionalBits),
            frac_src_offset);

  // Copy result to double-float buffer, FFT (freq-analyze) it at high-res
  double magn_signal, magn_other;
  MeasureAudioFreq(accum.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq,
                   &magn_signal, &magn_other);

  // Calculate Signal-to-Noise-And-Distortion (SINAD)
  // We can directly compare 'signal' and 'other', regardless of source format.
  *sinad_db = ValToDb(magn_signal / magn_other);

  // All sources (8-bit, 16-bit, ...) are normalized to "int18" in accum buffer.
  return ValToDb(magn_signal / expected_amplitude);
}

// Measure level response and noise floor for 1kHz sine from 8-bit source.
TEST(NoiseFloor, Source_8) {
  AudioResult::LevelSource8 =
      MeasureSourceNoiseFloor<uint8_t>(&AudioResult::FloorSource8);

  EXPECT_NEAR(AudioResult::LevelSource8, 0.0,
              AudioResult::kPrevLevelToleranceSource8);
  AudioResult::LevelToleranceSource8 =
      fmax(AudioResult::LevelToleranceSource8, abs(AudioResult::LevelSource8));

  EXPECT_GE(AudioResult::FloorSource8, AudioResult::kPrevFloorSource8);
}

// Measure level response and noise floor for 1kHz sine from 16bit source.
TEST(NoiseFloor, Source_16) {
  AudioResult::LevelSource16 =
      MeasureSourceNoiseFloor<int16_t>(&AudioResult::FloorSource16);

  EXPECT_NEAR(AudioResult::LevelSource16, 0.0,
              AudioResult::kPrevLevelToleranceSource16);
  AudioResult::LevelToleranceSource16 = fmax(
      AudioResult::LevelToleranceSource16, abs(AudioResult::LevelSource16));

  EXPECT_GE(AudioResult::FloorSource16, AudioResult::kPrevFloorSource16);
}

// Measure level response and noise floor for 1kHz sine from 16bit source.
TEST(NoiseFloor, Source_Float) {
  AudioResult::LevelSourceFloat =
      MeasureSourceNoiseFloor<float>(&AudioResult::FloorSourceFloat);

  EXPECT_NEAR(AudioResult::LevelSourceFloat, 0.0,
              AudioResult::kPrevLevelToleranceSourceFloat);
  AudioResult::LevelToleranceSourceFloat =
      fmax(AudioResult::LevelToleranceSourceFloat,
           abs(AudioResult::LevelSourceFloat));

  EXPECT_GE(AudioResult::FloorSourceFloat, AudioResult::kPrevFloorSourceFloat);
}

// Calculate magnitude of primary signal strength, compared to max value. Do the
// same for noise level, compared to the received signal.  For 8-bit output,
// using int8::max (not uint8::max) is intentional, as within uint8 we still use
// a maximum amplitude of 127 (it is just centered on 128). For float, we
// populate the accumulator with full-range vals that translate to [-1.0, +1.0].
template <typename T>
double MeasureOutputNoiseFloor(double* sinad_db) {
  OutputFormatterPtr output_formatter;
  double amplitude, expected_amplitude;

  // Calculate expected magnitude of signal strength, compared to max value.
  // For 8-bit output, compensate for the shift it got on the way to accum.
  // N.B.: using int8::max (not uint8::max) is intentional, as within uint8
  // we still use a maximum amplitude of 127 (it is just centered on 128).
  // For float, 7FFF equates to less than 1.0, so adjust by (32768/32767).

  if (std::is_same<T, uint8_t>::value) {
    output_formatter =
        SelectOutputFormatter(fuchsia::media::AudioSampleFormat::UNSIGNED_8, 1);
    expected_amplitude = std::numeric_limits<int8_t>::max();
    amplitude = expected_amplitude * (1 << (kAudioPipelineWidth - 8));
  } else if (std::is_same<T, int16_t>::value) {
    output_formatter =
        SelectOutputFormatter(fuchsia::media::AudioSampleFormat::SIGNED_16, 1);
    expected_amplitude = std::numeric_limits<int16_t>::max();
    amplitude = expected_amplitude * (1 << (kAudioPipelineWidth - 16));
  } else if (std::is_same<T, float>::value) {
    output_formatter =
        SelectOutputFormatter(fuchsia::media::AudioSampleFormat::FLOAT, 1);
    expected_amplitude = 1.0;
    amplitude = expected_amplitude * (1 << (kAudioPipelineWidth - 1));
  } else {
    FXL_DCHECK(false) << "Unsupported source format";
  }

  // Populate accum buffer and output to destination buffer
  std::vector<int32_t> accum(kFreqTestBufSize);
  OverwriteCosine(accum.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq,
                  amplitude);

  std::vector<T> dest(kFreqTestBufSize);
  output_formatter->ProduceOutput(accum.data(), dest.data(), kFreqTestBufSize);

  // Copy result to double-float buffer, FFT (freq-analyze) it at high-res
  double magn_signal, magn_other;
  MeasureAudioFreq(dest.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq,
                   &magn_signal, &magn_other);

  // Calculate Signal-to-Noise-And-Distortion (SINAD)
  // We can directly compare 'signal' and 'other', regardless of output format.
  *sinad_db = ValToDb(magn_signal / magn_other);

  return ValToDb(magn_signal / expected_amplitude);
}

// Measure level response and noise floor for 1kHz sine, to an 8bit output.
TEST(NoiseFloor, Output_8) {
  AudioResult::LevelOutput8 =
      MeasureOutputNoiseFloor<uint8_t>(&AudioResult::FloorOutput8);

  EXPECT_NEAR(AudioResult::LevelOutput8, 0.0,
              AudioResult::kPrevLevelToleranceOutput8);
  AudioResult::LevelToleranceOutput8 =
      fmax(AudioResult::LevelToleranceOutput8, abs(AudioResult::LevelOutput8));

  EXPECT_GE(AudioResult::FloorOutput8, AudioResult::kPrevFloorOutput8);
}

// Measure level response and noise floor for 1kHz sine, to a 16bit output.
TEST(NoiseFloor, Output_16) {
  AudioResult::LevelOutput16 =
      MeasureOutputNoiseFloor<int16_t>(&AudioResult::FloorOutput16);

  EXPECT_NEAR(AudioResult::LevelOutput16, 0.0,
              AudioResult::kPrevLevelToleranceOutput16);
  AudioResult::LevelToleranceOutput16 = fmax(
      AudioResult::LevelToleranceOutput16, abs(AudioResult::LevelOutput16));

  EXPECT_GE(AudioResult::FloorOutput16, AudioResult::kPrevFloorOutput16);
}

// Measure level response and noise floor for 1kHz sine, to a 16bit output.
TEST(NoiseFloor, Output_Float) {
  AudioResult::LevelOutputFloat =
      MeasureOutputNoiseFloor<float>(&AudioResult::FloorOutputFloat);

  EXPECT_NEAR(AudioResult::LevelOutputFloat, 0.0,
              AudioResult::kPrevLevelToleranceOutputFloat);
  AudioResult::LevelToleranceOutputFloat =
      fmax(AudioResult::LevelToleranceOutputFloat,
           abs(AudioResult::LevelOutputFloat));

  EXPECT_GE(AudioResult::FloorOutputFloat, AudioResult::kPrevFloorOutputFloat);
}

// Ideal frequency response measurement is 0.00 dB across the audible spectrum
// Ideal SINAD is at least 6 dB per signal-bit (>96 dB, if 16-bit resolution).
// If UseFullFrequencySet is false, we test at only three summary frequencies.
void MeasureFreqRespSinad(MixerPtr mixer, uint32_t src_buf_size,
                          double* level_db, double* sinad_db) {
  if (!std::isnan(level_db[0])) {
    // This run already has frequency response and SINAD test results for this
    // sampler and resampling ratio; don't waste time and cycles rerunning it.
    return;
  }
  // Set this to a valid (worst-case) value, so that (for any outcome) another
  // test does not later rerun this combination of sampler and resample ratio.
  level_db[0] = -INFINITY;

  // Vector source[] has an additional element because depending on resampling
  // ratio, some resamplers need it in order to produce the final dest value.
  // All FFT inputs are considered periodic, so to generate a periodic output
  // from the resampler, this extra source element should equal source[0].
  std::vector<float> source(src_buf_size + 1);
  std::vector<int32_t> accum(kFreqTestBufSize);
  uint32_t step_size = (Mixer::FRAC_ONE * src_buf_size) / kFreqTestBufSize;
  uint32_t modulo =
      (Mixer::FRAC_ONE * src_buf_size) - (step_size * kFreqTestBufSize);

  // kReferenceFreqs[] contains the full set of official test frequencies (47).
  // The "summary" list is a small subset (3) of that list. Each kSummaryIdxs[]
  // value is an index (in kReferenceFreqs[]) to one of those frequencies.
  uint32_t num_freqs = FrequencySet::UseFullFrequencySet
                           ? FrequencySet::kReferenceFreqs.size()
                           : FrequencySet::kSummaryIdxs.size();

  // Measure level response for each frequency.
  for (uint32_t idx = 0; idx < num_freqs; ++idx) {
    // If full-spectrum testing, test at every frequency in kReferenceFreqs[];
    // otherwise, only use the frequencies indicated in kSummaryIdxs[].
    uint32_t freq_idx = idx;
    if (FrequencySet::UseFullFrequencySet == false) {
      freq_idx = FrequencySet::kSummaryIdxs[idx];
    }

    // If frequency is too high to be characterized in this buffer, skip it.
    // Per Nyquist, buffer length must be at least 2x the measured frequency.
    if (FrequencySet::kReferenceFreqs[freq_idx] * 2 > src_buf_size) {
      continue;
    }

    // Populate the source buffer with a sinusoid at each reference frequency.
    OverwriteCosine(source.data(), src_buf_size,
                    FrequencySet::kReferenceFreqs[freq_idx]);
    source[src_buf_size] = source[0];

    // Resample the source into the accumulation buffer, in pieces. (Why in
    // pieces? See description of kResamplerTestNumPackets in frequency_set.h.)
    uint32_t dst_frames, dst_offset;
    int32_t frac_src_offset;
    uint32_t frac_src_frames = source.size() * Mixer::FRAC_ONE;

    for (uint32_t packet = 0; packet < kResamplerTestNumPackets; ++packet) {
      dst_frames = kFreqTestBufSize * (packet + 1) / kResamplerTestNumPackets;
      dst_offset = kFreqTestBufSize * packet / kResamplerTestNumPackets;
      frac_src_offset =
          (static_cast<int64_t>(src_buf_size) * Mixer::FRAC_ONE * packet) /
          kResamplerTestNumPackets;

      mixer->Mix(accum.data(), dst_frames, &dst_offset, source.data(),
                 frac_src_frames, &frac_src_offset, step_size,
                 Gain::kUnityScale, false, modulo, kFreqTestBufSize);
      EXPECT_EQ(dst_frames, dst_offset);
    }

    // Copy results to double[], for high-resolution frequency analysis (FFT).
    double magn_signal = -INFINITY, magn_other = INFINITY;
    MeasureAudioFreq(accum.data(), kFreqTestBufSize,
                     FrequencySet::kReferenceFreqs[freq_idx], &magn_signal,
                     &magn_other);

    // Calculate Frequency Response and Signal-to-Noise-And-Distortion (SINAD).
    level_db[freq_idx] =
        ValToDb(magn_signal / (1 << (kAudioPipelineWidth - 1)));
    sinad_db[freq_idx] = ValToDb(magn_signal / magn_other);
  }
}

// Given result and limit arrays, compare them as frequency response results.
// I.e., ensure greater-than-or-equal-to, plus a less-than-or-equal-to check
// against the overall level tolerance (for level results greater than 0 dB).
void EvaluateFreqRespResults(double* freq_resp_results,
                             const double* freq_resp_limits) {
  uint32_t num_freqs = FrequencySet::UseFullFrequencySet
                           ? FrequencySet::kReferenceFreqs.size()
                           : FrequencySet::kSummaryIdxs.size();

  for (uint32_t idx = 0; idx < num_freqs; ++idx) {
    uint32_t freq = FrequencySet::UseFullFrequencySet
                        ? idx
                        : FrequencySet::kSummaryIdxs[idx];
    EXPECT_GE(freq_resp_results[freq], freq_resp_limits[freq]) << freq;
    EXPECT_LE(freq_resp_results[freq],
              0.0 + AudioResult::kPrevLevelToleranceInterpolation)
        << freq;
    AudioResult::LevelToleranceInterpolation =
        fmax(AudioResult::LevelToleranceInterpolation, freq_resp_results[freq]);
  }
}

// Given result and limit arrays, compare them as SINAD results. This simply
// means apply a strict greater-than-or-equal-to, without additional tolerance.
void EvaluateSinadResults(double* sinad_results, const double* sinad_limits) {
  uint32_t num_freqs = FrequencySet::UseFullFrequencySet
                           ? FrequencySet::kReferenceFreqs.size()
                           : FrequencySet::kSummaryIdxs.size();

  for (uint32_t idx = 0; idx < num_freqs; ++idx) {
    uint32_t freq = FrequencySet::UseFullFrequencySet
                        ? idx
                        : FrequencySet::kSummaryIdxs[idx];
    EXPECT_GE(sinad_results[freq], sinad_limits[freq]) << freq;
  }
}

// For the given resampler, measure frequency response and sinad at unity (no
// SRC). We articulate this with source buffer length equal to dest length.
void TestUnitySampleRatio(Resampler sampler_type, double* freq_resp_results,
                          double* sinad_results) {
  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1,
                               48000, 1, 48000, sampler_type);

  MeasureFreqRespSinad(std::move(mixer), kFreqTestBufSize, freq_resp_results,
                       sinad_results);
}

// For the given resampler, target a 2:1 downsampling ratio. We articulate this
// by specifying a source buffer twice the length of the destination buffer.
void TestDownSampleRatio1(Resampler sampler_type, double* freq_resp_results,
                          double* sinad_results) {
  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1,
                               96000, 1, 48000, sampler_type);

  MeasureFreqRespSinad(std::move(mixer),
                       round(kFreqTestBufSize * 96000.0 / 48000.0),
                       freq_resp_results, sinad_results);
}

// For the given resampler, target 88200->48000 downsampling. We articulate this
// by specifying a source buffer longer than destination buffer by that ratio.
void TestDownSampleRatio2(Resampler sampler_type, double* freq_resp_results,
                          double* sinad_results) {
  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1,
                               88200, 1, 48000, sampler_type);

  MeasureFreqRespSinad(std::move(mixer),
                       round(kFreqTestBufSize * 88200.0 / 48000.0),
                       freq_resp_results, sinad_results);
}

// For the given resampler, target 44100->48000 upsampling. We articulate this
// by specifying a source buffer shorter than destination buffer by that ratio.
void TestUpSampleRatio1(Resampler sampler_type, double* freq_resp_results,
                        double* sinad_results) {
  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1,
                               44100, 1, 48000, sampler_type);

  MeasureFreqRespSinad(std::move(mixer),
                       round(kFreqTestBufSize * 44100.0 / 48000.0),
                       freq_resp_results, sinad_results);
}

// For the given resampler, target the 1:2 upsampling ratio. We articulate this
// by specifying a source buffer at half the length of the destination buffer.
void TestUpSampleRatio2(Resampler sampler_type, double* freq_resp_results,
                        double* sinad_results) {
  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1,
                               24000, 1, 48000, sampler_type);

  MeasureFreqRespSinad(std::move(mixer),
                       round(kFreqTestBufSize * 24000.0 / 48000.0),
                       freq_resp_results, sinad_results);
}

// For the given resampler, target micro-sampling -- with a 47999:48000 ratio.
void TestMicroSampleRatio(Resampler sampler_type, double* freq_resp_results,
                          double* sinad_results) {
  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1,
                               47999, 1, 48000, sampler_type);

  MeasureFreqRespSinad(std::move(mixer),
                       round(kFreqTestBufSize * 47999.0 / 48000.0),
                       freq_resp_results, sinad_results);
}

// Measure Freq Response for Point sampler, no rate conversion.
TEST(FrequencyResponse, Point_Unity) {
  TestUnitySampleRatio(Resampler::SampleAndHold,
                       AudioResult::FreqRespPointUnity.data(),
                       AudioResult::SinadPointUnity.data());

  EvaluateFreqRespResults(AudioResult::FreqRespPointUnity.data(),
                          AudioResult::kPrevFreqRespPointUnity.data());
}

// Measure SINAD for Point sampler, no rate conversion.
TEST(Sinad, Point_Unity) {
  TestUnitySampleRatio(Resampler::SampleAndHold,
                       AudioResult::FreqRespPointUnity.data(),
                       AudioResult::SinadPointUnity.data());

  EvaluateSinadResults(AudioResult::SinadPointUnity.data(),
                       AudioResult::kPrevSinadPointUnity.data());
}

// Measure Freq Response for Point sampler, first down-sampling ratio.
TEST(FrequencyResponse, Point_DownSamp1) {
  TestDownSampleRatio1(Resampler::SampleAndHold,
                       AudioResult::FreqRespPointDown1.data(),
                       AudioResult::SinadPointDown1.data());

  EvaluateFreqRespResults(AudioResult::FreqRespPointDown1.data(),
                          AudioResult::kPrevFreqRespPointDown1.data());
}

// Measure SINAD for Point sampler, first down-sampling ratio.
TEST(Sinad, Point_DownSamp1) {
  TestDownSampleRatio1(Resampler::SampleAndHold,
                       AudioResult::FreqRespPointDown1.data(),
                       AudioResult::SinadPointDown1.data());

  EvaluateSinadResults(AudioResult::SinadPointDown1.data(),
                       AudioResult::kPrevSinadPointDown1.data());
}

// Measure Freq Response for Point sampler, second down-sampling ratio.
TEST(FrequencyResponse, Point_DownSamp2) {
  TestDownSampleRatio2(Resampler::SampleAndHold,
                       AudioResult::FreqRespPointDown2.data(),
                       AudioResult::SinadPointDown2.data());

  EvaluateFreqRespResults(AudioResult::FreqRespPointDown2.data(),
                          AudioResult::kPrevFreqRespPointDown2.data());
}

// Measure SINAD for Point sampler, second down-sampling ratio.
TEST(Sinad, Point_DownSamp2) {
  TestDownSampleRatio2(Resampler::SampleAndHold,
                       AudioResult::FreqRespPointDown2.data(),
                       AudioResult::SinadPointDown2.data());

  EvaluateSinadResults(AudioResult::SinadPointDown2.data(),
                       AudioResult::kPrevSinadPointDown2.data());
}

// Measure Freq Response for Point sampler, first up-sampling ratio.
TEST(FrequencyResponse, Point_UpSamp1) {
  TestUpSampleRatio1(Resampler::SampleAndHold,
                     AudioResult::FreqRespPointUp1.data(),
                     AudioResult::SinadPointUp1.data());

  EvaluateFreqRespResults(AudioResult::FreqRespPointUp1.data(),
                          AudioResult::kPrevFreqRespPointUp1.data());
}

// Measure SINAD for Point sampler, first up-sampling ratio.
TEST(Sinad, Point_UpSamp1) {
  TestUpSampleRatio1(Resampler::SampleAndHold,
                     AudioResult::FreqRespPointUp1.data(),
                     AudioResult::SinadPointUp1.data());

  EvaluateSinadResults(AudioResult::SinadPointUp1.data(),
                       AudioResult::kPrevSinadPointUp1.data());
}

// Measure Freq Response for Point sampler, second up-sampling ratio.
TEST(FrequencyResponse, Point_UpSamp2) {
  TestUpSampleRatio2(Resampler::SampleAndHold,
                     AudioResult::FreqRespPointUp2.data(),
                     AudioResult::SinadPointUp2.data());

  EvaluateFreqRespResults(AudioResult::FreqRespPointUp2.data(),
                          AudioResult::kPrevFreqRespPointUp2.data());
}

// Measure SINAD for Point sampler, second up-sampling ratio.
TEST(Sinad, Point_UpSamp2) {
  TestUpSampleRatio2(Resampler::SampleAndHold,
                     AudioResult::FreqRespPointUp2.data(),
                     AudioResult::SinadPointUp2.data());

  EvaluateSinadResults(AudioResult::SinadPointUp2.data(),
                       AudioResult::kPrevSinadPointUp2.data());
}

// Measure Freq Response for Point sampler with minimum rate change.
TEST(FrequencyResponse, Point_MicroSRC) {
  TestMicroSampleRatio(Resampler::SampleAndHold,
                       AudioResult::FreqRespPointMicro.data(),
                       AudioResult::SinadPointMicro.data());

  EvaluateFreqRespResults(AudioResult::FreqRespPointMicro.data(),
                          AudioResult::kPrevFreqRespPointMicro.data());
}

// Measure SINAD for Point sampler with minimum rate change.
TEST(Sinad, Point_MicroSRC) {
  TestMicroSampleRatio(Resampler::SampleAndHold,
                       AudioResult::FreqRespPointMicro.data(),
                       AudioResult::SinadPointMicro.data());

  EvaluateSinadResults(AudioResult::SinadPointMicro.data(),
                       AudioResult::kPrevSinadPointMicro.data());
}

// Measure Freq Response for Point sampler, no rate conversion.
TEST(FrequencyResponse, Linear_Unity) {
  TestUnitySampleRatio(Resampler::LinearInterpolation,
                       AudioResult::FreqRespLinearUnity.data(),
                       AudioResult::SinadLinearUnity.data());

  EvaluateFreqRespResults(AudioResult::FreqRespLinearUnity.data(),
                          AudioResult::kPrevFreqRespLinearUnity.data());
}

// Measure SINAD for Point sampler, no rate conversion.
TEST(Sinad, Linear_Unity) {
  TestUnitySampleRatio(Resampler::LinearInterpolation,
                       AudioResult::FreqRespLinearUnity.data(),
                       AudioResult::SinadLinearUnity.data());

  EvaluateSinadResults(AudioResult::SinadLinearUnity.data(),
                       AudioResult::kPrevSinadLinearUnity.data());
}

// Measure Freq Response for Linear sampler, first down-sampling ratio.
TEST(FrequencyResponse, Linear_DownSamp1) {
  TestDownSampleRatio1(Resampler::LinearInterpolation,
                       AudioResult::FreqRespLinearDown1.data(),
                       AudioResult::SinadLinearDown1.data());

  EvaluateFreqRespResults(AudioResult::FreqRespLinearDown1.data(),
                          AudioResult::kPrevFreqRespLinearDown1.data());
}

// Measure SINAD for Linear sampler, first down-sampling ratio.
TEST(Sinad, Linear_DownSamp1) {
  TestDownSampleRatio1(Resampler::LinearInterpolation,
                       AudioResult::FreqRespLinearDown1.data(),
                       AudioResult::SinadLinearDown1.data());

  EvaluateSinadResults(AudioResult::SinadLinearDown1.data(),
                       AudioResult::kPrevSinadLinearDown1.data());
}

// Measure Freq Response for Linear sampler, second down-sampling ratio.
TEST(FrequencyResponse, Linear_DownSamp2) {
  TestDownSampleRatio2(Resampler::LinearInterpolation,
                       AudioResult::FreqRespLinearDown2.data(),
                       AudioResult::SinadLinearDown2.data());

  EvaluateFreqRespResults(AudioResult::FreqRespLinearDown2.data(),
                          AudioResult::kPrevFreqRespLinearDown2.data());
}

// Measure SINAD for Linear sampler, second down-sampling ratio.
TEST(Sinad, Linear_DownSamp2) {
  TestDownSampleRatio2(Resampler::LinearInterpolation,
                       AudioResult::FreqRespLinearDown2.data(),
                       AudioResult::SinadLinearDown2.data());

  EvaluateSinadResults(AudioResult::SinadLinearDown2.data(),
                       AudioResult::kPrevSinadLinearDown2.data());
}

// Measure Freq Response for Linear sampler, first up-sampling ratio.
TEST(FrequencyResponse, Linear_UpSamp1) {
  TestUpSampleRatio1(Resampler::LinearInterpolation,
                     AudioResult::FreqRespLinearUp1.data(),
                     AudioResult::SinadLinearUp1.data());

  EvaluateFreqRespResults(AudioResult::FreqRespLinearUp1.data(),
                          AudioResult::kPrevFreqRespLinearUp1.data());
}

// Measure SINAD for Linear sampler, first up-sampling ratio.
TEST(Sinad, Linear_UpSamp1) {
  TestUpSampleRatio1(Resampler::LinearInterpolation,
                     AudioResult::FreqRespLinearUp1.data(),
                     AudioResult::SinadLinearUp1.data());

  EvaluateSinadResults(AudioResult::SinadLinearUp1.data(),
                       AudioResult::kPrevSinadLinearUp1.data());
}

// Measure Freq Response for Linear sampler, second up-sampling ratio.
TEST(FrequencyResponse, Linear_UpSamp2) {
  TestUpSampleRatio2(Resampler::LinearInterpolation,
                     AudioResult::FreqRespLinearUp2.data(),
                     AudioResult::SinadLinearUp2.data());

  EvaluateFreqRespResults(AudioResult::FreqRespLinearUp2.data(),
                          AudioResult::kPrevFreqRespLinearUp2.data());
}

// Measure SINAD for Linear sampler, second up-sampling ratio.
TEST(Sinad, Linear_UpSamp2) {
  TestUpSampleRatio2(Resampler::LinearInterpolation,
                     AudioResult::FreqRespLinearUp2.data(),
                     AudioResult::SinadLinearUp2.data());

  EvaluateSinadResults(AudioResult::SinadLinearUp2.data(),
                       AudioResult::kPrevSinadLinearUp2.data());
}

// Measure Freq Response for Linear sampler with minimum rate change.
TEST(FrequencyResponse, Linear_MicroSRC) {
  TestMicroSampleRatio(Resampler::LinearInterpolation,
                       AudioResult::FreqRespLinearMicro.data(),
                       AudioResult::SinadLinearMicro.data());

  EvaluateFreqRespResults(AudioResult::FreqRespLinearMicro.data(),
                          AudioResult::kPrevFreqRespLinearMicro.data());
}

// Measure SINAD for Linear sampler with minimum rate change.
TEST(Sinad, Linear_MicroSRC) {
  TestMicroSampleRatio(Resampler::LinearInterpolation,
                       AudioResult::FreqRespLinearMicro.data(),
                       AudioResult::SinadLinearMicro.data());

  EvaluateSinadResults(AudioResult::SinadLinearMicro.data(),
                       AudioResult::kPrevSinadLinearMicro.data());
}

}  // namespace test
}  // namespace audio
}  // namespace media
