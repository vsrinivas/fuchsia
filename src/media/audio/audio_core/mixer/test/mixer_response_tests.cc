// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <iomanip>

#include "src/media/audio/audio_core/mixer/test/audio_result.h"
#include "src/media/audio/audio_core/mixer/test/frequency_set.h"
#include "src/media/audio/audio_core/mixer/test/mixer_tests_shared.h"
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio::test {

// Convenience abbreviation within this source file to shorten names
using Resampler = ::media::audio::Mixer::Resampler;

//
// Baseline Noise-Floor tests
//
// These tests determine our best-case audio quality/fidelity, in the absence of any gain,
// interpolation/SRC, mixing, reformatting or other processing. These tests are done with a single
// 1kHz tone, and provide a baseline from which we can measure any changes in sonic quality caused
// by other mixer stages.
//
// In performing all of our audio analysis tests with a specific buffer length, we can choose input
// sinusoids with frequencies that perfectly fit within those buffers (eliminating the need for FFT
// windowing). The reference frequency below was specifically designed as an approximation of a 1kHz
// tone, assuming an eventual 48kHz output sample rate.
template <typename T>
double MeasureSourceNoiseFloor(double* sinad_db) {
  std::unique_ptr<Mixer> mixer;
  double amplitude, expected_amplitude;

  if constexpr (std::is_same_v<T, uint8_t>) {
    mixer = SelectMixer(fuchsia::media::AudioSampleFormat::UNSIGNED_8, 1, 48000, 1, 48000,
                        Resampler::SampleAndHold);
    amplitude = kFullScaleInt8InputAmplitude;
    expected_amplitude = kFullScaleInt8AccumAmplitude;
  } else if constexpr (std::is_same_v<T, int16_t>) {
    mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                        Resampler::SampleAndHold);
    amplitude = kFullScaleInt16InputAmplitude;
    expected_amplitude = kFullScaleInt16AccumAmplitude;
  } else if constexpr (std::is_same_v<T, int32_t>) {
    mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, 1, 48000, 1, 48000,
                        Resampler::SampleAndHold);
    amplitude = kFullScaleInt24In32InputAmplitude;
    expected_amplitude = kFullScaleInt24In32AccumAmplitude;
  } else if constexpr (std::is_same_v<T, float>) {
    mixer = SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, 48000, 1, 48000,
                        Resampler::SampleAndHold);
    amplitude = kFullScaleFloatInputAmplitude;
    expected_amplitude = kFullScaleFloatAccumAmplitude;
  } else {
    FX_DCHECK(false) << "Unsupported source format";
  }

  // Populate source buffer; mix it (pass-thru) to accumulation buffer
  std::vector<T> source(kFreqTestBufSize);
  OverwriteCosine(source.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq, amplitude);

  std::vector<float> accum(kFreqTestBufSize);
  uint32_t dest_offset = 0;
  uint32_t frac_src_frames = kFreqTestBufSize << kPtsFractionalBits;

  // First "prime" the resampler by sending a mix command exactly at the end of the source buffer.
  // This allows it to cache the frames at buffer's end. For our testing, buffers are periodic, so
  // these frames are exactly what would have immediately preceded the first data in the buffer.
  // This enables resamplers with significant side width to perform as they would in steady-state.
  int32_t frac_src_offset = frac_src_frames;
  auto source_is_consumed = mixer->Mix(accum.data(), kFreqTestBufSize, &dest_offset, source.data(),
                                       frac_src_frames, &frac_src_offset, false);
  FX_DCHECK(source_is_consumed);
  FX_DCHECK(dest_offset == 0u);
  FX_DCHECK(frac_src_offset == static_cast<int32_t>(frac_src_frames));

  // We now have a full cache of previous frames (for resamplers that require this), so do the mix.
  frac_src_offset = 0;
  mixer->Mix(accum.data(), kFreqTestBufSize, &dest_offset, source.data(), frac_src_frames,
             &frac_src_offset, false);
  EXPECT_EQ(dest_offset, kFreqTestBufSize);
  EXPECT_EQ(frac_src_offset, static_cast<int32_t>(frac_src_frames));

  // Copy result to double-float buffer, FFT (freq-analyze) it at high-res
  double magn_signal, magn_other;
  MeasureAudioFreq(accum.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq, &magn_signal,
                   &magn_other);

  // Convert Signal-to-Noise-And-Distortion (SINAD) to decibels
  // We can directly compare 'signal' and 'other', regardless of source format.
  *sinad_db = Gain::DoubleToDb(magn_signal / magn_other);

  // All sources (8-bit, 16-bit, ...) are normalized to float in accum buffer.
  return Gain::DoubleToDb(magn_signal / expected_amplitude);
}

// Measure level response and noise floor for 1kHz sine from 8-bit source.
TEST(NoiseFloor, Source_8) {
  AudioResult::LevelSource8 = MeasureSourceNoiseFloor<uint8_t>(&AudioResult::FloorSource8);

  EXPECT_NEAR(AudioResult::LevelSource8, 0.0, AudioResult::kPrevLevelToleranceSource8);
  AudioResult::LevelToleranceSource8 =
      fmax(AudioResult::LevelToleranceSource8, abs(AudioResult::LevelSource8));

  EXPECT_GE(AudioResult::FloorSource8, AudioResult::kPrevFloorSource8)
      << std::setprecision(10) << AudioResult::FloorSource8;
}

// Measure level response and noise floor for 1kHz sine from 16-bit source.
TEST(NoiseFloor, Source_16) {
  AudioResult::LevelSource16 = MeasureSourceNoiseFloor<int16_t>(&AudioResult::FloorSource16);

  EXPECT_NEAR(AudioResult::LevelSource16, 0.0, AudioResult::kPrevLevelToleranceSource16);
  AudioResult::LevelToleranceSource16 =
      fmax(AudioResult::LevelToleranceSource16, abs(AudioResult::LevelSource16));

  EXPECT_GE(AudioResult::FloorSource16, AudioResult::kPrevFloorSource16)
      << std::setprecision(10) << AudioResult::FloorSource16;
}

// Measure level response and noise floor for 1kHz sine from 24-bit source.
TEST(NoiseFloor, Source_24) {
  AudioResult::LevelSource24 = MeasureSourceNoiseFloor<int32_t>(&AudioResult::FloorSource24);

  EXPECT_NEAR(AudioResult::LevelSource24, 0.0, AudioResult::kPrevLevelToleranceSource24);
  AudioResult::LevelToleranceSource24 =
      fmax(AudioResult::LevelToleranceSource24, abs(AudioResult::LevelSource24));

  EXPECT_GE(AudioResult::FloorSource24, AudioResult::kPrevFloorSource24)
      << std::setprecision(10) << AudioResult::FloorSource24;
}

// Measure level response and noise floor for 1kHz sine from float source.
TEST(NoiseFloor, Source_Float) {
  AudioResult::LevelSourceFloat = MeasureSourceNoiseFloor<float>(&AudioResult::FloorSourceFloat);

  EXPECT_NEAR(AudioResult::LevelSourceFloat, 0.0, AudioResult::kPrevLevelToleranceSourceFloat);
  AudioResult::LevelToleranceSourceFloat =
      fmax(AudioResult::LevelToleranceSourceFloat, abs(AudioResult::LevelSourceFloat));

  EXPECT_GE(AudioResult::FloorSourceFloat, AudioResult::kPrevFloorSourceFloat)
      << std::setprecision(10) << AudioResult::FloorSourceFloat;
}

// Calculate magnitude of primary signal strength, compared to max value. Do the same for noise
// level, compared to the received signal.  For 8-bit output, using int8::max (not uint8::max) is
// intentional, as within uint8 we still use a maximum amplitude of 127 (it is just centered on
// 128). For float, we populate the accumulator with full-range vals that translate to [-1.0, +1.0].
template <typename T>
double MeasureOutputNoiseFloor(double* sinad_db) {
  std::unique_ptr<OutputProducer> output_producer;
  double amplitude, expected_amplitude;

  // Calculate expected magnitude of signal strength, compared to max value. For 8-bit output,
  // compensate for the shift it got on the way to accum. N.B.: using int8::max (not uint8::max) is
  // intentional, as within uint8 we still use a maximum amplitude of 127 (it is just centered on
  // 128). For float, 7FFF equates to less than 1.0, so adjust by (32768/32767).

  if (std::is_same_v<T, uint8_t>) {
    output_producer = SelectOutputProducer(fuchsia::media::AudioSampleFormat::UNSIGNED_8, 1);
    expected_amplitude = kFullScaleInt8InputAmplitude;
    amplitude = kFullScaleInt8AccumAmplitude;
  } else if (std::is_same_v<T, int16_t>) {
    output_producer = SelectOutputProducer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1);
    expected_amplitude = kFullScaleInt16InputAmplitude;
    amplitude = kFullScaleInt16AccumAmplitude;
  } else if (std::is_same_v<T, int32_t>) {
    output_producer = SelectOutputProducer(fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, 1);
    expected_amplitude = kFullScaleInt24In32InputAmplitude;
    amplitude = kFullScaleInt24In32AccumAmplitude;
  } else if (std::is_same_v<T, float>) {
    output_producer = SelectOutputProducer(fuchsia::media::AudioSampleFormat::FLOAT, 1);
    expected_amplitude = kFullScaleFloatInputAmplitude;
    amplitude = kFullScaleFloatAccumAmplitude;
  } else {
    FX_DCHECK(false) << "Unsupported source format";
  }

  // Populate accum buffer and output to destination buffer
  std::vector<float> accum(kFreqTestBufSize);
  OverwriteCosine(accum.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq, amplitude);

  std::vector<T> dest(kFreqTestBufSize);
  output_producer->ProduceOutput(accum.data(), dest.data(), kFreqTestBufSize);

  // Copy result to double-float buffer, FFT (freq-analyze) it at high-res
  double magn_signal, magn_other;
  MeasureAudioFreq(dest.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq, &magn_signal,
                   &magn_other);

  // Convert Signal-to-Noise-And-Distortion (SINAD) to decibels.
  // We can directly compare 'signal' and 'other', regardless of output format.
  *sinad_db = Gain::DoubleToDb(magn_signal / magn_other);

  return Gain::DoubleToDb(magn_signal / expected_amplitude);
}

// Measure level response and noise floor for 1kHz sine, to an 8-bit output.
TEST(NoiseFloor, Output_8) {
  AudioResult::LevelOutput8 = MeasureOutputNoiseFloor<uint8_t>(&AudioResult::FloorOutput8);

  EXPECT_NEAR(AudioResult::LevelOutput8, 0.0, AudioResult::kPrevLevelToleranceOutput8);
  AudioResult::LevelToleranceOutput8 =
      fmax(AudioResult::LevelToleranceOutput8, abs(AudioResult::LevelOutput8));

  EXPECT_GE(AudioResult::FloorOutput8, AudioResult::kPrevFloorOutput8)
      << std::setprecision(10) << AudioResult::FloorOutput8;
}

// Measure level response and noise floor for 1kHz sine, to a 16-bit output.
TEST(NoiseFloor, Output_16) {
  AudioResult::LevelOutput16 = MeasureOutputNoiseFloor<int16_t>(&AudioResult::FloorOutput16);

  EXPECT_NEAR(AudioResult::LevelOutput16, 0.0, AudioResult::kPrevLevelToleranceOutput16);
  AudioResult::LevelToleranceOutput16 =
      fmax(AudioResult::LevelToleranceOutput16, abs(AudioResult::LevelOutput16));

  EXPECT_GE(AudioResult::FloorOutput16, AudioResult::kPrevFloorOutput16)
      << std::setprecision(10) << AudioResult::FloorOutput16;
}

// Measure level response and noise floor for 1kHz sine, to a 24-bit output.
TEST(NoiseFloor, Output_24) {
  AudioResult::LevelOutput24 = MeasureOutputNoiseFloor<int32_t>(&AudioResult::FloorOutput24);

  EXPECT_NEAR(AudioResult::LevelOutput24, 0.0, AudioResult::kPrevLevelToleranceOutput24);
  AudioResult::LevelToleranceOutput24 =
      fmax(AudioResult::LevelToleranceOutput24, abs(AudioResult::LevelOutput24));

  EXPECT_GE(AudioResult::FloorOutput24, AudioResult::kPrevFloorOutput24)
      << std::setprecision(10) << AudioResult::FloorOutput24;
}

// Measure level response and noise floor for 1kHz sine, to a float output.
TEST(NoiseFloor, Output_Float) {
  AudioResult::LevelOutputFloat = MeasureOutputNoiseFloor<float>(&AudioResult::FloorOutputFloat);

  EXPECT_NEAR(AudioResult::LevelOutputFloat, 0.0, AudioResult::kPrevLevelToleranceOutputFloat);
  AudioResult::LevelToleranceOutputFloat =
      fmax(AudioResult::LevelToleranceOutputFloat, abs(AudioResult::LevelOutputFloat));

  EXPECT_GE(AudioResult::FloorOutputFloat, AudioResult::kPrevFloorOutputFloat)
      << std::setprecision(10) << AudioResult::FloorOutputFloat;
}

// Ideal frequency response measurement is 0.00 dB across the audible spectrum
//
// Ideal SINAD is at least 6 dB per signal-bit (>96 dB, if 16-bit resolution).
//
// Phase measurement is the amount that a certain frequency is delayed -- measured in radians,
// because after a delay of more than its wavelength, a frequency's perceptible delay "wraps around"
// to a value 2_PI less. Zero phase is ideal; constant phase is very good; linear is reasonable.
//
// If UseFullFrequencySet is false, we test at only three summary frequencies.
void MeasureFreqRespSinadPhase(Mixer* mixer, uint32_t num_src_frames, double* level_db,
                               double* sinad_db, double* phase_rad) {
  if (!std::isnan(level_db[0])) {
    // This run already has frequency response/SINAD/phase results for this sampler and resampling
    // ratio; don't waste time and cycles rerunning it.
    return;
  }
  // Set this to a valid (worst-case) value, so that (for any outcome) another test does not later
  // rerun this combination of sampler and resample ratio.
  level_db[0] = -INFINITY;

  auto num_dest_frames = kFreqTestBufSize;
  // Some resamplers need additional data in order to produce the final values, and the amount of
  // data can change depending on resampling ratio. However, all FFT inputs are considered periodic,
  // so to generate a periodic output from the resampler, we can provide extra source elements to
  // resamplers by simply wrapping around to source[0], etc.
  std::vector<float> source(num_src_frames);
  std::vector<float> accum(num_dest_frames);

  // We use this to keep ongoing src_pos_modulo across multiple Mix() calls.
  auto& info = mixer->bookkeeping();
  info.step_size = (Mixer::FRAC_ONE * num_src_frames) / num_dest_frames;
  info.rate_modulo = (Mixer::FRAC_ONE * num_src_frames) - (info.step_size * num_dest_frames);
  info.denominator = num_dest_frames;

  bool use_full_set = FrequencySet::UseFullFrequencySet;
  // kReferenceFreqs[] contains the full set of test frequencies (47). kSummaryIdxs is a subset of
  // 3 -- each kSummaryIdxs[] value is an index (in kReferenceFreqs[]) to one of those frequencies.
  const auto first_idx = 0u;
  // const auto first_idx = use_full_set ? FrequencySet::kFirstInBandRefFreqIdx : 0u;
  const auto last_idx =
      use_full_set ? FrequencySet::kNumReferenceFreqs : FrequencySet::kSummaryIdxs.size();
  // use_full_set ? FrequencySet::kFirstOutBandRefFreqIdx : FrequencySet::kSummaryIdxs.size();

  // Generate signal, rate-convert, and measure level and phase responses -- for each frequency.
  for (auto idx = first_idx; idx < last_idx; ++idx) {
    // If full-spectrum, test at all kReferenceFreqs[] values; else only use those in kSummaryIdxs[]
    uint32_t freq_idx = idx;
    if (!use_full_set) {
      freq_idx = FrequencySet::kSummaryIdxs[idx];
    }
    auto frequency_to_measure = FrequencySet::kReferenceFreqs[freq_idx];

    // If frequency is too high to be characterized in this buffer, skip it. Per Nyquist limit,
    // buffer length must be at least 2x the frequency we want to measure.
    if (frequency_to_measure * 2 >= num_src_frames) {
      if (freq_idx < FrequencySet::kFirstOutBandRefFreqIdx) {
        level_db[freq_idx] = -INFINITY;
        phase_rad[freq_idx] = -INFINITY;
      }
      sinad_db[freq_idx] = -INFINITY;
      continue;
    }

    // Populate the source buffer with a sinusoid at each reference frequency.
    OverwriteCosine(source.data(), num_src_frames, frequency_to_measure);

    // Use this to keep ongoing src_pos_modulo across multiple Mix() calls, but then reset it each
    // time we start testing a new input signal frequency.
    info.src_pos_modulo = 0;

    uint32_t dest_frames, dest_offset = 0;
    uint32_t frac_src_frames = source.size() * Mixer::FRAC_ONE;

    // First "prime" the resampler by sending a mix command exactly at the end of the source buffer.
    // This allows it to cache the frames at buffer's end. For our testing, buffers are periodic, so
    // these frames are exactly what would have immediately preceded the first data in the buffer.
    // This enables resamplers with significant side width to perform as they would in steady-state.
    int32_t frac_src_offset = static_cast<int32_t>(frac_src_frames);
    auto source_is_consumed = mixer->Mix(accum.data(), num_dest_frames, &dest_offset, source.data(),
                                         frac_src_frames, &frac_src_offset, false);
    FX_CHECK(source_is_consumed);
    FX_CHECK(dest_offset == 0u);
    FX_CHECK(frac_src_offset == static_cast<int32_t>(frac_src_frames));

    // Now resample source to accum. (Why in pieces? See kResamplerTestNumPackets: frequency_set.h)
    frac_src_offset = 0;
    for (uint32_t packet = 0; packet < kResamplerTestNumPackets; ++packet) {
      dest_frames = num_dest_frames * (packet + 1) / kResamplerTestNumPackets;
      mixer->Mix(accum.data(), dest_frames, &dest_offset, source.data(), frac_src_frames,
                 &frac_src_offset, false);
    }

    int32_t expected_frac_src_offset = frac_src_frames;
    if (dest_offset < dest_frames) {
      AUD_VLOG(SPEW) << "Performing wraparound mix: dest_frames " << dest_frames << ", dest_offset "
                     << dest_offset << ", frac_src_frames " << std::hex << frac_src_frames
                     << ", frac_src_offset " << frac_src_offset;
      ASSERT_GE(frac_src_offset, 0);
      EXPECT_GE(static_cast<uint32_t>(frac_src_offset) + mixer->pos_filter_width().raw_value(),
                frac_src_frames)
          << "src_off " << std::hex << frac_src_offset << ", pos_width "
          << mixer->pos_filter_width().raw_value() << ", src_frames " << frac_src_frames;

      // Wrap around in the source buffer -- making the offset slightly negative. We can do
      // this, within the positive filter width of this sampler.
      frac_src_offset -= frac_src_frames;
      mixer->Mix(accum.data(), dest_frames, &dest_offset, source.data(), frac_src_frames,
                 &frac_src_offset, false);
      expected_frac_src_offset = 0;
    }
    EXPECT_EQ(dest_offset, dest_frames);
    EXPECT_EQ(frac_src_offset, expected_frac_src_offset);

    // After running each frequency, clear the cached filter state. This is not strictly necessary
    // today, since each frequency test starts precisely at buffer-start (thus for Point and Linear
    // resamplers, no previously-cached state is needed). However, this IS a requirement for future
    // resamplers with larger positive filter widths (they exposed the bug); address this now.
    mixer->Reset();

    // Copy results to double[], for high-resolution frequency analysis (FFT).
    double magn_signal = -INFINITY, magn_other = INFINITY;
    MeasureAudioFreq(accum.data(), num_dest_frames, frequency_to_measure, &magn_signal, &magn_other,
                     &phase_rad[freq_idx]);

    // Convert Frequency Response and Signal-to-Noise-And-Distortion (SINAD) to decibels.
    if (frequency_to_measure * 2 >= num_dest_frames) {
      // This out-of-band frequency should have been entirely rejected -- capture total magnitude.
      auto magn_total = std::sqrt(magn_signal * magn_signal + magn_other * magn_other);
      sinad_db[freq_idx] = -Gain::DoubleToDb(magn_total);
    } else {
      // This frequency is in-band -- capture its level as well as the magnitude of all else.
      level_db[freq_idx] = Gain::DoubleToDb(magn_signal);
      sinad_db[freq_idx] = Gain::DoubleToDb(magn_signal / magn_other);
    }
  }
}

// Given result and limit arrays, compare as frequency response results (must be greater-or-equal).
// Also perform a less-or-equal check against overall level tolerance (for level results greater
// than 0 dB). If 'summary_only', we limit evaluation to the three basic frequencies.
void EvaluateFreqRespResults(double* freq_resp_results, const double* freq_resp_limits,
                             bool summary_only = false) {
  bool use_full_set = (!summary_only) && FrequencySet::UseFullFrequencySet;
  const auto first_idx = use_full_set ? FrequencySet::kFirstInBandRefFreqIdx : 0u;
  const auto last_idx =
      use_full_set ? FrequencySet::kFirstOutBandRefFreqIdx : FrequencySet::kSummaryIdxs.size();

  for (auto idx = first_idx; idx < last_idx; ++idx) {
    uint32_t freq = use_full_set ? idx : FrequencySet::kSummaryIdxs[idx];

    EXPECT_GE(freq_resp_results[freq], freq_resp_limits[freq])
        << " [" << freq << "]  " << std::scientific << std::setprecision(9)
        << freq_resp_results[freq];
    EXPECT_LE(freq_resp_results[freq], 0.0 + AudioResult::kPrevLevelToleranceInterpolation)
        << " [" << freq << "]  " << std::scientific << std::setprecision(9)
        << freq_resp_results[freq];
    AudioResult::LevelToleranceInterpolation =
        fmax(AudioResult::LevelToleranceInterpolation, freq_resp_results[freq]);
  }
}

// Given result and limit arrays, compare as SINAD results (greater-or-equal, without additional
// tolerance). If 'summary_only', limit evaluation to the three basic frequencies.
void EvaluateSinadResults(double* sinad_results, const double* sinad_limits,
                          bool summary_only = false) {
  bool use_full_set = (!summary_only) && FrequencySet::UseFullFrequencySet;
  const auto first_idx = use_full_set ? FrequencySet::kFirstInBandRefFreqIdx : 0u;
  const auto last_idx =
      use_full_set ? FrequencySet::kFirstOutBandRefFreqIdx : FrequencySet::kSummaryIdxs.size();

  for (auto idx = first_idx; idx < last_idx; ++idx) {
    uint32_t freq = use_full_set ? idx : FrequencySet::kSummaryIdxs[idx];

    EXPECT_GE(sinad_results[freq], sinad_limits[freq])
        << " [" << freq << "]  " << std::scientific << std::setprecision(9) << sinad_results[freq];
  }
}

// Given result and limit arrays, compare rejection results (similar to SINAD, but out-of-band).
// There are no 'summary_only' frequencies for this scenario.
void EvaluateRejectionResults(double* rejection_results, const double* rejection_limits,
                              bool summary_only = false) {
  bool use_full_set = (!summary_only) && FrequencySet::UseFullFrequencySet;
  if (!use_full_set) {
    return;
  }

  for (uint32_t freq_idx = 0u; freq_idx < FrequencySet::kNumReferenceFreqs; ++freq_idx) {
    if (freq_idx < FrequencySet::kFirstInBandRefFreqIdx ||
        freq_idx >= FrequencySet::kFirstOutBandRefFreqIdx) {
      EXPECT_GE(rejection_results[freq_idx], rejection_limits[freq_idx])
          << " [" << freq_idx << "]  " << std::scientific << std::setprecision(9)
          << rejection_results[freq_idx];
    }
  }
}

// Given result and limit arrays, compare phase results (ensure that "was previously zero" stays
// that way). If 'summary_only', limit evaluation to the three basic frequencies.
void EvaluatePhaseResults(double* phase_results, const double* phase_limits,
                          bool summary_only = false) {
  auto use_full_set = (!summary_only) && FrequencySet::UseFullFrequencySet;
  const auto first_idx = use_full_set ? FrequencySet::kFirstInBandRefFreqIdx : 0u;
  const auto last_idx =
      use_full_set ? FrequencySet::kFirstOutBandRefFreqIdx : FrequencySet::kSummaryIdxs.size();

  for (auto idx = first_idx; idx < last_idx; ++idx) {
    auto freq = use_full_set ? idx : FrequencySet::kSummaryIdxs[idx];

    if (phase_limits[freq] == -INFINITY) {
      continue;
    }

    if ((phase_results[freq] - phase_limits[freq]) >= M_PI) {
      EXPECT_NEAR(phase_results[freq], phase_limits[freq] + (2.0 * M_PI),
                  AudioResult::kPhaseTolerance)
          << " [" << freq << "]  " << std::fixed << std::setprecision(5) << phase_results[freq];
    } else if ((phase_results[freq] - phase_limits[freq]) <= -M_PI) {
      EXPECT_NEAR(phase_results[freq], phase_limits[freq] - (2.0 * M_PI),
                  AudioResult::kPhaseTolerance)
          << " [" << freq << "]  " << std::fixed << std::setprecision(5) << phase_results[freq];
    } else {
      EXPECT_NEAR(phase_results[freq], phase_limits[freq], AudioResult::kPhaseTolerance)
          << " [" << freq << "]  " << std::fixed << std::setprecision(5) << phase_results[freq];
    }
  }
}

// For the given resampler, measure frequency response and sinad at unity (no SRC), articulated by
// source buffer length equal to dest length.
void TestUnitySampleRatio(Resampler sampler_type, double* freq_resp_results, double* sinad_results,
                          double* phase_results) {
  auto mixer =
      SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, 48000, 1, 48000, sampler_type);

  MeasureFreqRespSinadPhase(mixer.get(), kFreqTestBufSize, freq_resp_results, sinad_results,
                            phase_results);
}

// For the given resampler, target a 4:1 downsampling ratio, articulated by specifying a source
// buffer almost 4x the length of the destination. Note that because of the resampler filter width,
// we may ultimately "wraparound" and feed in the initial source data if we have not yet received
// the full amount of output data needed. The current buffer len (65536) x 8192 subframes/frame
// limits us to <4x SRC ratios.
void TestDownSampleRatio0(Resampler sampler_type, double* freq_resp_results, double* sinad_results,
                          double* phase_results) {
  auto mixer =
      SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, 191999, 1, 48000, sampler_type);

  MeasureFreqRespSinadPhase(mixer.get(), (kFreqTestBufSize << 2) - 1, freq_resp_results,
                            sinad_results, phase_results);
}

// For the given resampler, target a 2:1 downsampling ratio, articulated by specifying a source
// buffer twice the length of the destination buffer.
void TestDownSampleRatio1(Resampler sampler_type, double* freq_resp_results, double* sinad_results,
                          double* phase_results) {
  auto mixer =
      SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, 48000 * 2, 1, 48000, sampler_type);

  MeasureFreqRespSinadPhase(mixer.get(), kFreqTestBufSize << 1, freq_resp_results, sinad_results,
                            phase_results);
}

// For the given resampler, target 88200->48000 downsampling, articulated by specifying a source
// buffer longer than destination buffer by that ratio.
void TestDownSampleRatio2(Resampler sampler_type, double* freq_resp_results, double* sinad_results,
                          double* phase_results) {
  auto mixer =
      SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, 88200, 1, 48000, sampler_type);

  MeasureFreqRespSinadPhase(mixer.get(), round(kFreqTestBufSize * 88200.0 / 48000.0),
                            freq_resp_results, sinad_results, phase_results);
}

// For the given resampler, target micro-sampling -- with a 48001:48000 ratio.
void TestMicroSampleRatio(Resampler sampler_type, double* freq_resp_results, double* sinad_results,
                          double* phase_results) {
  auto mixer =
      SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, 48001, 1, 48000, sampler_type);

  MeasureFreqRespSinadPhase(mixer.get(), kFreqTestBufSize + 1, freq_resp_results, sinad_results,
                            phase_results);
}

// For the given resampler, target 44100->48000 upsampling, articulated by specifying a source
// buffer shorter than destination buffer by that ratio.
void TestUpSampleRatio1(Resampler sampler_type, double* freq_resp_results, double* sinad_results,
                        double* phase_results) {
  auto mixer =
      SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, 44100, 1, 48000, sampler_type);

  MeasureFreqRespSinadPhase(mixer.get(), round(kFreqTestBufSize * 44100.0 / 48000.0),
                            freq_resp_results, sinad_results, phase_results);
}

// For the given resampler, target the 1:2 upsampling ratio, articulated by specifying a source
// buffer at half the length of the destination buffer.
void TestUpSampleRatio2(Resampler sampler_type, double* freq_resp_results, double* sinad_results,
                        double* phase_results) {
  auto mixer =
      SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, 24000, 1, 24000 * 2, sampler_type);

  MeasureFreqRespSinadPhase(mixer.get(), kFreqTestBufSize >> 1, freq_resp_results, sinad_results,
                            phase_results);
}

// For this resampler, target the upsampling ratio "almost 1:4". EXACTLY 1:4 (combined with our
// chosen buffer size, and the system definition of STEP_SIZE), would exceed MAX_INT for src_pos. We
// specify a source buffer at _just_greater__than_ 1/4 the length of the destination buffer.
void TestUpSampleRatio3(Resampler sampler_type, double* freq_resp_results, double* sinad_results,
                        double* phase_results) {
  auto mixer =
      SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1, 12001, 1, 48000, sampler_type);

  MeasureFreqRespSinadPhase(mixer.get(), (kFreqTestBufSize >> 2) + 1, freq_resp_results,
                            sinad_results, phase_results);
}

// Measure Freq Response for Point sampler, no rate conversion.
TEST(FrequencyResponse, Point_Unity) {
  TestUnitySampleRatio(Resampler::SampleAndHold, AudioResult::FreqRespPointUnity.data(),
                       AudioResult::SinadPointUnity.data(), AudioResult::PhasePointUnity.data());

  EvaluateFreqRespResults(AudioResult::FreqRespPointUnity.data(),
                          AudioResult::kPrevFreqRespPointUnity.data());
}

// Measure SINAD for Point sampler, no rate conversion.
TEST(Sinad, Point_Unity) {
  TestUnitySampleRatio(Resampler::SampleAndHold, AudioResult::FreqRespPointUnity.data(),
                       AudioResult::SinadPointUnity.data(), AudioResult::PhasePointUnity.data());

  EvaluateSinadResults(AudioResult::SinadPointUnity.data(),
                       AudioResult::kPrevSinadPointUnity.data());
}

// Measure Phase Response for Point sampler, no rate conversion.
TEST(Phase, Point_Unity) {
  TestUnitySampleRatio(Resampler::SampleAndHold, AudioResult::FreqRespPointUnity.data(),
                       AudioResult::SinadPointUnity.data(), AudioResult::PhasePointUnity.data());

  EvaluatePhaseResults(AudioResult::PhasePointUnity.data(),
                       AudioResult::kPrevPhasePointUnity.data());
}

// Measure Freq Response for Point sampler for down-sampling ratio #0.
TEST(FrequencyResponse, Point_DownSamp0) {
  TestDownSampleRatio0(Resampler::SampleAndHold, AudioResult::FreqRespPointDown0.data(),
                       AudioResult::SinadPointDown0.data(), AudioResult::PhasePointDown0.data());

  EvaluateFreqRespResults(AudioResult::FreqRespPointDown0.data(),
                          AudioResult::kPrevFreqRespPointDown0.data());
}

// Measure SINAD for Point sampler for down-sampling ratio #0.
TEST(Sinad, Point_DownSamp0) {
  TestDownSampleRatio0(Resampler::SampleAndHold, AudioResult::FreqRespPointDown0.data(),
                       AudioResult::SinadPointDown0.data(), AudioResult::PhasePointDown0.data());

  EvaluateSinadResults(AudioResult::SinadPointDown0.data(),
                       AudioResult::kPrevSinadPointDown0.data());
}

// Measure Out-of-band Rejection for Point sampler for down-sampling ratio #0.
TEST(Rejection, Point_DownSamp0) {
  TestDownSampleRatio0(Resampler::SampleAndHold, AudioResult::FreqRespPointDown0.data(),
                       AudioResult::SinadPointDown0.data(), AudioResult::PhasePointDown0.data());

  EvaluateRejectionResults(AudioResult::SinadPointDown0.data(),
                           AudioResult::kPrevSinadPointDown0.data());
}

// Measure Phase Response for Point sampler for down-sampling ratio #0.
TEST(Phase, Point_DownSamp0) {
  TestDownSampleRatio0(Resampler::SampleAndHold, AudioResult::FreqRespPointDown0.data(),
                       AudioResult::SinadPointDown0.data(), AudioResult::PhasePointDown0.data());

  EvaluatePhaseResults(AudioResult::PhasePointDown0.data(),
                       AudioResult::kPrevPhasePointDown0.data());
}

// Measure Freq Response for Point sampler for down-sampling ratio #1.
TEST(FrequencyResponse, Point_DownSamp1) {
  TestDownSampleRatio1(Resampler::SampleAndHold, AudioResult::FreqRespPointDown1.data(),
                       AudioResult::SinadPointDown1.data(), AudioResult::PhasePointDown1.data());

  EvaluateFreqRespResults(AudioResult::FreqRespPointDown1.data(),
                          AudioResult::kPrevFreqRespPointDown1.data());
}

// Measure SINAD for Point sampler for down-sampling ratio #1.
TEST(Sinad, Point_DownSamp1) {
  TestDownSampleRatio1(Resampler::SampleAndHold, AudioResult::FreqRespPointDown1.data(),
                       AudioResult::SinadPointDown1.data(), AudioResult::PhasePointDown1.data());

  EvaluateSinadResults(AudioResult::SinadPointDown1.data(),
                       AudioResult::kPrevSinadPointDown1.data());
}

// Measure Out-of-band Rejection for Point sampler for down-sampling ratio #1.
TEST(Rejection, Point_DownSamp1) {
  TestDownSampleRatio1(Resampler::SampleAndHold, AudioResult::FreqRespPointDown1.data(),
                       AudioResult::SinadPointDown1.data(), AudioResult::PhasePointDown1.data());

  EvaluateRejectionResults(AudioResult::SinadPointDown1.data(),
                           AudioResult::kPrevSinadPointDown1.data());
}

// Measure Phase Response for Point sampler for down-sampling ratio #1.
TEST(Phase, Point_DownSamp1) {
  TestDownSampleRatio1(Resampler::SampleAndHold, AudioResult::FreqRespPointDown1.data(),
                       AudioResult::SinadPointDown1.data(), AudioResult::PhasePointDown1.data());

  EvaluatePhaseResults(AudioResult::PhasePointDown1.data(),
                       AudioResult::kPrevPhasePointDown1.data());
}

// Measure Freq Response for Point sampler for down-sampling ratio #2.
TEST(FrequencyResponse, Point_DownSamp2) {
  TestDownSampleRatio2(Resampler::SampleAndHold, AudioResult::FreqRespPointDown2.data(),
                       AudioResult::SinadPointDown2.data(), AudioResult::PhasePointDown2.data());

  EvaluateFreqRespResults(AudioResult::FreqRespPointDown2.data(),
                          AudioResult::kPrevFreqRespPointDown2.data());
}

// Measure SINAD for Point sampler for down-sampling ratio #2.
TEST(Sinad, Point_DownSamp2) {
  TestDownSampleRatio2(Resampler::SampleAndHold, AudioResult::FreqRespPointDown2.data(),
                       AudioResult::SinadPointDown2.data(), AudioResult::PhasePointDown2.data());

  EvaluateSinadResults(AudioResult::SinadPointDown2.data(),
                       AudioResult::kPrevSinadPointDown2.data());
}

// Measure Out-of-band Rejection for Point sampler for down-sampling ratio #2.
TEST(Rejection, Point_DownSamp2) {
  TestDownSampleRatio2(Resampler::SampleAndHold, AudioResult::FreqRespPointDown2.data(),
                       AudioResult::SinadPointDown2.data(), AudioResult::PhasePointDown2.data());

  EvaluateRejectionResults(AudioResult::SinadPointDown2.data(),
                           AudioResult::kPrevSinadPointDown2.data());
}

// Measure Phase Response for Point sampler for down-sampling ratio #2.
TEST(Phase, Point_DownSamp2) {
  TestDownSampleRatio2(Resampler::SampleAndHold, AudioResult::FreqRespPointDown2.data(),
                       AudioResult::SinadPointDown2.data(), AudioResult::PhasePointDown2.data());

  EvaluatePhaseResults(AudioResult::PhasePointDown2.data(),
                       AudioResult::kPrevPhasePointDown2.data());
}

// Measure Freq Response for Point sampler with minimum down-sampling rate change.
TEST(FrequencyResponse, Point_MicroSRC) {
  TestMicroSampleRatio(Resampler::SampleAndHold, AudioResult::FreqRespPointMicro.data(),
                       AudioResult::SinadPointMicro.data(), AudioResult::PhasePointMicro.data());

  EvaluateFreqRespResults(AudioResult::FreqRespPointMicro.data(),
                          AudioResult::kPrevFreqRespPointMicro.data());
}

// Measure SINAD for Point sampler with minimum down-sampling rate change.
TEST(Sinad, Point_MicroSRC) {
  TestMicroSampleRatio(Resampler::SampleAndHold, AudioResult::FreqRespPointMicro.data(),
                       AudioResult::SinadPointMicro.data(), AudioResult::PhasePointMicro.data());

  EvaluateSinadResults(AudioResult::SinadPointMicro.data(),
                       AudioResult::kPrevSinadPointMicro.data());
}

// Measure Out-of-band Rejection for Point sampler with minimum down-sampling rate change.
TEST(Rejection, Point_MicroSRC) {
  TestMicroSampleRatio(Resampler::SampleAndHold, AudioResult::FreqRespPointMicro.data(),
                       AudioResult::SinadPointMicro.data(), AudioResult::PhasePointMicro.data());

  EvaluateRejectionResults(AudioResult::SinadPointMicro.data(),
                           AudioResult::kPrevSinadPointMicro.data());
}

// Measure Phase Response for Point sampler with minimum down-sampling rate change.
TEST(Phase, Point_MicroSRC) {
  TestMicroSampleRatio(Resampler::SampleAndHold, AudioResult::FreqRespPointMicro.data(),
                       AudioResult::SinadPointMicro.data(), AudioResult::PhasePointMicro.data());

  EvaluatePhaseResults(AudioResult::PhasePointMicro.data(),
                       AudioResult::kPrevPhasePointMicro.data());
}

// Measure Freq Response for Point sampler for up-sampling ratio #1.
TEST(FrequencyResponse, Point_UpSamp1) {
  TestUpSampleRatio1(Resampler::SampleAndHold, AudioResult::FreqRespPointUp1.data(),
                     AudioResult::SinadPointUp1.data(), AudioResult::PhasePointUp1.data());

  EvaluateFreqRespResults(AudioResult::FreqRespPointUp1.data(),
                          AudioResult::kPrevFreqRespPointUp1.data());
}

// Measure SINAD for Point sampler for up-sampling ratio #1.
TEST(Sinad, Point_UpSamp1) {
  TestUpSampleRatio1(Resampler::SampleAndHold, AudioResult::FreqRespPointUp1.data(),
                     AudioResult::SinadPointUp1.data(), AudioResult::PhasePointUp1.data());

  EvaluateSinadResults(AudioResult::SinadPointUp1.data(), AudioResult::kPrevSinadPointUp1.data());
}

// Measure Phase Response for Point sampler for up-sampling ratio #1.
TEST(Phase, Point_UpSamp1) {
  TestUpSampleRatio1(Resampler::SampleAndHold, AudioResult::FreqRespPointUp1.data(),
                     AudioResult::SinadPointUp1.data(), AudioResult::PhasePointUp1.data());

  EvaluatePhaseResults(AudioResult::PhasePointUp1.data(), AudioResult::kPrevPhasePointUp1.data());
}

// Measure Freq Response for Point sampler for up-sampling ratio #2.
TEST(FrequencyResponse, Point_UpSamp2) {
  TestUpSampleRatio2(Resampler::SampleAndHold, AudioResult::FreqRespPointUp2.data(),
                     AudioResult::SinadPointUp2.data(), AudioResult::PhasePointUp2.data());

  EvaluateFreqRespResults(AudioResult::FreqRespPointUp2.data(),
                          AudioResult::kPrevFreqRespPointUp2.data());
}

// Measure SINAD for Point sampler for up-sampling ratio #2.
TEST(Sinad, Point_UpSamp2) {
  TestUpSampleRatio2(Resampler::SampleAndHold, AudioResult::FreqRespPointUp2.data(),
                     AudioResult::SinadPointUp2.data(), AudioResult::PhasePointUp2.data());

  EvaluateSinadResults(AudioResult::SinadPointUp2.data(), AudioResult::kPrevSinadPointUp2.data());
}

// Measure Phase Response for Point sampler for up-sampling ratio #2.
TEST(Phase, Point_UpSamp2) {
  TestUpSampleRatio2(Resampler::SampleAndHold, AudioResult::FreqRespPointUp2.data(),
                     AudioResult::SinadPointUp2.data(), AudioResult::PhasePointUp2.data());

  EvaluatePhaseResults(AudioResult::PhasePointUp2.data(), AudioResult::kPrevPhasePointUp2.data());
}

// Measure Freq Response for Point sampler for up-sampling ratio #3.
TEST(FrequencyResponse, Point_UpSamp3) {
  TestUpSampleRatio3(Resampler::SampleAndHold, AudioResult::FreqRespPointUp3.data(),
                     AudioResult::SinadPointUp3.data(), AudioResult::PhasePointUp3.data());

  EvaluateFreqRespResults(AudioResult::FreqRespPointUp3.data(),
                          AudioResult::kPrevFreqRespPointUp3.data());
}

// Measure SINAD for Point sampler for up-sampling ratio #3.
TEST(Sinad, Point_UpSamp3) {
  TestUpSampleRatio3(Resampler::SampleAndHold, AudioResult::FreqRespPointUp3.data(),
                     AudioResult::SinadPointUp3.data(), AudioResult::PhasePointUp3.data());

  EvaluateSinadResults(AudioResult::SinadPointUp3.data(), AudioResult::kPrevSinadPointUp3.data());
}

// Measure Phase Response for Point sampler for up-sampling ratio #3.
TEST(Phase, Point_UpSamp3) {
  TestUpSampleRatio3(Resampler::SampleAndHold, AudioResult::FreqRespPointUp3.data(),
                     AudioResult::SinadPointUp3.data(), AudioResult::PhasePointUp3.data());

  EvaluatePhaseResults(AudioResult::PhasePointUp3.data(), AudioResult::kPrevPhasePointUp3.data());
}

// Measure Freq Response for Linear sampler, no rate conversion.
TEST(FrequencyResponse, Linear_Unity) {
  TestUnitySampleRatio(Resampler::LinearInterpolation, AudioResult::FreqRespLinearUnity.data(),
                       AudioResult::SinadLinearUnity.data(), AudioResult::PhaseLinearUnity.data());

  EvaluateFreqRespResults(AudioResult::FreqRespLinearUnity.data(),
                          AudioResult::kPrevFreqRespLinearUnity.data());
}

// Measure SINAD for Linear sampler, no rate conversion.
TEST(Sinad, Linear_Unity) {
  TestUnitySampleRatio(Resampler::LinearInterpolation, AudioResult::FreqRespLinearUnity.data(),
                       AudioResult::SinadLinearUnity.data(), AudioResult::PhaseLinearUnity.data());

  EvaluateSinadResults(AudioResult::SinadLinearUnity.data(),
                       AudioResult::kPrevSinadLinearUnity.data());
}

// Measure Phase Response for Linear sampler, no rate conversion.
TEST(Phase, Linear_Unity) {
  TestUnitySampleRatio(Resampler::LinearInterpolation, AudioResult::FreqRespLinearUnity.data(),
                       AudioResult::SinadLinearUnity.data(), AudioResult::PhaseLinearUnity.data());

  EvaluatePhaseResults(AudioResult::PhaseLinearUnity.data(),
                       AudioResult::kPrevPhaseLinearUnity.data());
}

// Measure Freq Response for Linear sampler for down-sampling ratio #0.
TEST(FrequencyResponse, Linear_DownSamp0) {
  TestDownSampleRatio0(Resampler::LinearInterpolation, AudioResult::FreqRespLinearDown0.data(),
                       AudioResult::SinadLinearDown0.data(), AudioResult::PhaseLinearDown0.data());

  EvaluateFreqRespResults(AudioResult::FreqRespLinearDown0.data(),
                          AudioResult::kPrevFreqRespLinearDown0.data());
}

// Measure SINAD for Linear sampler for down-sampling ratio #0.
TEST(Sinad, Linear_DownSamp0) {
  TestDownSampleRatio0(Resampler::LinearInterpolation, AudioResult::FreqRespLinearDown0.data(),
                       AudioResult::SinadLinearDown0.data(), AudioResult::PhaseLinearDown0.data());

  EvaluateSinadResults(AudioResult::SinadLinearDown0.data(),
                       AudioResult::kPrevSinadLinearDown0.data());
}

// Measure Out-of-band Rejection for Linear sampler for down-sampling ratio #0.
TEST(Rejection, Linear_DownSamp0) {
  TestDownSampleRatio0(Resampler::LinearInterpolation, AudioResult::FreqRespLinearDown0.data(),
                       AudioResult::SinadLinearDown0.data(), AudioResult::PhaseLinearDown0.data());

  EvaluateRejectionResults(AudioResult::SinadLinearDown0.data(),
                           AudioResult::kPrevSinadLinearDown0.data());
}

// Measure Phase Response for Linear sampler for down-sampling ratio #0.
TEST(Phase, Linear_DownSamp0) {
  TestDownSampleRatio0(Resampler::LinearInterpolation, AudioResult::FreqRespLinearDown0.data(),
                       AudioResult::SinadLinearDown0.data(), AudioResult::PhaseLinearDown0.data());

  EvaluatePhaseResults(AudioResult::PhaseLinearDown0.data(),
                       AudioResult::kPrevPhaseLinearDown0.data());
}

// Measure Freq Response for Linear sampler for down-sampling ratio #1.
TEST(FrequencyResponse, Linear_DownSamp1) {
  TestDownSampleRatio1(Resampler::LinearInterpolation, AudioResult::FreqRespLinearDown1.data(),
                       AudioResult::SinadLinearDown1.data(), AudioResult::PhaseLinearDown1.data());

  EvaluateFreqRespResults(AudioResult::FreqRespLinearDown1.data(),
                          AudioResult::kPrevFreqRespLinearDown1.data());
}

// Measure SINAD for Linear sampler for down-sampling ratio #1.
TEST(Sinad, Linear_DownSamp1) {
  TestDownSampleRatio1(Resampler::LinearInterpolation, AudioResult::FreqRespLinearDown1.data(),
                       AudioResult::SinadLinearDown1.data(), AudioResult::PhaseLinearDown1.data());

  EvaluateSinadResults(AudioResult::SinadLinearDown1.data(),
                       AudioResult::kPrevSinadLinearDown1.data());
}

// Measure Out-of-band Rejection for Linear sampler for down-sampling ratio #1.
TEST(Rejection, Linear_DownSamp1) {
  TestDownSampleRatio1(Resampler::LinearInterpolation, AudioResult::FreqRespLinearDown1.data(),
                       AudioResult::SinadLinearDown1.data(), AudioResult::PhaseLinearDown1.data());

  EvaluateRejectionResults(AudioResult::SinadLinearDown1.data(),
                           AudioResult::kPrevSinadLinearDown1.data());
}

// Measure Phase Response for Linear sampler for down-sampling ratio #1.
TEST(Phase, Linear_DownSamp1) {
  TestDownSampleRatio1(Resampler::LinearInterpolation, AudioResult::FreqRespLinearDown1.data(),
                       AudioResult::SinadLinearDown1.data(), AudioResult::PhaseLinearDown1.data());

  EvaluatePhaseResults(AudioResult::PhaseLinearDown1.data(),
                       AudioResult::kPrevPhaseLinearDown1.data());
}

// Measure Freq Response for Linear sampler for down-sampling ratio #2.
TEST(FrequencyResponse, Linear_DownSamp2) {
  TestDownSampleRatio2(Resampler::LinearInterpolation, AudioResult::FreqRespLinearDown2.data(),
                       AudioResult::SinadLinearDown2.data(), AudioResult::PhaseLinearDown2.data());

  EvaluateFreqRespResults(AudioResult::FreqRespLinearDown2.data(),
                          AudioResult::kPrevFreqRespLinearDown2.data());
}

// Measure SINAD for Linear sampler for down-sampling ratio #2.
TEST(Sinad, Linear_DownSamp2) {
  TestDownSampleRatio2(Resampler::LinearInterpolation, AudioResult::FreqRespLinearDown2.data(),
                       AudioResult::SinadLinearDown2.data(), AudioResult::PhaseLinearDown2.data());

  EvaluateSinadResults(AudioResult::SinadLinearDown2.data(),
                       AudioResult::kPrevSinadLinearDown2.data());
}

// Measure Out-of-band Rejection for Linear sampler for down-sampling ratio #2.
TEST(Rejection, Linear_DownSamp2) {
  TestDownSampleRatio2(Resampler::LinearInterpolation, AudioResult::FreqRespLinearDown2.data(),
                       AudioResult::SinadLinearDown2.data(), AudioResult::PhaseLinearDown2.data());

  EvaluateRejectionResults(AudioResult::SinadLinearDown2.data(),
                           AudioResult::kPrevSinadLinearDown2.data());
}

// Measure Phase Response for Linear sampler for down-sampling ratio #2.
TEST(Phase, Linear_DownSamp2) {
  TestDownSampleRatio2(Resampler::LinearInterpolation, AudioResult::FreqRespLinearDown2.data(),
                       AudioResult::SinadLinearDown2.data(), AudioResult::PhaseLinearDown2.data());

  EvaluatePhaseResults(AudioResult::PhaseLinearDown2.data(),
                       AudioResult::kPrevPhaseLinearDown2.data());
}

// Measure Freq Response for Linear sampler with minimum down-sampling rate change.
TEST(FrequencyResponse, Linear_MicroSRC) {
  TestMicroSampleRatio(Resampler::LinearInterpolation, AudioResult::FreqRespLinearMicro.data(),
                       AudioResult::SinadLinearMicro.data(), AudioResult::PhaseLinearMicro.data());

  EvaluateFreqRespResults(AudioResult::FreqRespLinearMicro.data(),
                          AudioResult::kPrevFreqRespLinearMicro.data());
}

// Measure SINAD for Linear sampler with minimum down-sampling rate change.
TEST(Sinad, Linear_MicroSRC) {
  TestMicroSampleRatio(Resampler::LinearInterpolation, AudioResult::FreqRespLinearMicro.data(),
                       AudioResult::SinadLinearMicro.data(), AudioResult::PhaseLinearMicro.data());

  EvaluateSinadResults(AudioResult::SinadLinearMicro.data(),
                       AudioResult::kPrevSinadLinearMicro.data());
}

// Measure Out-of-band Rejection for Linear sampler with minimum down-sampling rate change.
TEST(Rejection, Linear_MicroSRC) {
  TestMicroSampleRatio(Resampler::LinearInterpolation, AudioResult::FreqRespLinearMicro.data(),
                       AudioResult::SinadLinearMicro.data(), AudioResult::PhaseLinearMicro.data());

  EvaluateRejectionResults(AudioResult::SinadLinearMicro.data(),
                           AudioResult::kPrevSinadLinearMicro.data());
}

// Measure Phase Response for Linear sampler with minimum down-sampling rate change.
TEST(Phase, Linear_MicroSRC) {
  TestMicroSampleRatio(Resampler::LinearInterpolation, AudioResult::FreqRespLinearMicro.data(),
                       AudioResult::SinadLinearMicro.data(), AudioResult::PhaseLinearMicro.data());

  EvaluatePhaseResults(AudioResult::PhaseLinearMicro.data(),
                       AudioResult::kPrevPhaseLinearMicro.data());
}

// Measure Freq Response for Linear sampler for up-sampling ratio #1.
TEST(FrequencyResponse, Linear_UpSamp1) {
  TestUpSampleRatio1(Resampler::LinearInterpolation, AudioResult::FreqRespLinearUp1.data(),
                     AudioResult::SinadLinearUp1.data(), AudioResult::PhaseLinearUp1.data());

  EvaluateFreqRespResults(AudioResult::FreqRespLinearUp1.data(),
                          AudioResult::kPrevFreqRespLinearUp1.data());
}

// Measure SINAD for Linear sampler for up-sampling ratio #1.
TEST(Sinad, Linear_UpSamp1) {
  TestUpSampleRatio1(Resampler::LinearInterpolation, AudioResult::FreqRespLinearUp1.data(),
                     AudioResult::SinadLinearUp1.data(), AudioResult::PhaseLinearUp1.data());

  EvaluateSinadResults(AudioResult::SinadLinearUp1.data(), AudioResult::kPrevSinadLinearUp1.data());
}

// Measure Phase Response for Linear sampler for up-sampling ratio #1.
TEST(Phase, Linear_UpSamp1) {
  TestUpSampleRatio1(Resampler::LinearInterpolation, AudioResult::FreqRespLinearUp1.data(),
                     AudioResult::SinadLinearUp1.data(), AudioResult::PhaseLinearUp1.data());

  EvaluatePhaseResults(AudioResult::PhaseLinearUp1.data(), AudioResult::kPrevPhaseLinearUp1.data());
}

// Measure Freq Response for Linear sampler for up-sampling ratio #2.
TEST(FrequencyResponse, Linear_UpSamp2) {
  TestUpSampleRatio2(Resampler::LinearInterpolation, AudioResult::FreqRespLinearUp2.data(),
                     AudioResult::SinadLinearUp2.data(), AudioResult::PhaseLinearUp2.data());

  EvaluateFreqRespResults(AudioResult::FreqRespLinearUp2.data(),
                          AudioResult::kPrevFreqRespLinearUp2.data());
}

// Measure SINAD for Linear sampler for up-sampling ratio #2.
TEST(Sinad, Linear_UpSamp2) {
  TestUpSampleRatio2(Resampler::LinearInterpolation, AudioResult::FreqRespLinearUp2.data(),
                     AudioResult::SinadLinearUp2.data(), AudioResult::PhaseLinearUp2.data());

  EvaluateSinadResults(AudioResult::SinadLinearUp2.data(), AudioResult::kPrevSinadLinearUp2.data());
}

// Measure Phase Response for Linear sampler for up-sampling ratio #2.
TEST(Phase, Linear_UpSamp2) {
  TestUpSampleRatio2(Resampler::LinearInterpolation, AudioResult::FreqRespLinearUp2.data(),
                     AudioResult::SinadLinearUp2.data(), AudioResult::PhaseLinearUp2.data());

  EvaluatePhaseResults(AudioResult::PhaseLinearUp2.data(), AudioResult::kPrevPhaseLinearUp2.data());
}

// Measure Freq Response for Linear sampler for up-sampling ratio #3.
TEST(FrequencyResponse, Linear_UpSamp3) {
  TestUpSampleRatio3(Resampler::LinearInterpolation, AudioResult::FreqRespLinearUp3.data(),
                     AudioResult::SinadLinearUp3.data(), AudioResult::PhaseLinearUp3.data());

  EvaluateFreqRespResults(AudioResult::FreqRespLinearUp3.data(),
                          AudioResult::kPrevFreqRespLinearUp3.data());
}

// Measure SINAD for Linear sampler for up-sampling ratio #3.
TEST(Sinad, Linear_UpSamp3) {
  TestUpSampleRatio3(Resampler::LinearInterpolation, AudioResult::FreqRespLinearUp3.data(),
                     AudioResult::SinadLinearUp3.data(), AudioResult::PhaseLinearUp3.data());

  EvaluateSinadResults(AudioResult::SinadLinearUp3.data(), AudioResult::kPrevSinadLinearUp3.data());
}

// Measure Phase Response for Linear sampler for up-sampling ratio #3.
TEST(Phase, Linear_UpSamp3) {
  TestUpSampleRatio3(Resampler::LinearInterpolation, AudioResult::FreqRespLinearUp3.data(),
                     AudioResult::SinadLinearUp3.data(), AudioResult::PhaseLinearUp3.data());

  EvaluatePhaseResults(AudioResult::PhaseLinearUp3.data(), AudioResult::kPrevPhaseLinearUp3.data());
}

// Measure Freq Response for Sinc sampler, no rate conversion.
TEST(FrequencyResponse, Sinc_Unity) {
  TestUnitySampleRatio(Resampler::WindowedSinc, AudioResult::FreqRespSincUnity.data(),
                       AudioResult::SinadSincUnity.data(), AudioResult::PhaseSincUnity.data());

  EvaluateFreqRespResults(AudioResult::FreqRespSincUnity.data(),
                          AudioResult::kPrevFreqRespSincUnity.data());
}

// Measure SINAD for Sinc sampler, no rate conversion.
TEST(Sinad, Sinc_Unity) {
  TestUnitySampleRatio(Resampler::WindowedSinc, AudioResult::FreqRespSincUnity.data(),
                       AudioResult::SinadSincUnity.data(), AudioResult::PhaseSincUnity.data());

  EvaluateSinadResults(AudioResult::SinadSincUnity.data(), AudioResult::kPrevSinadSincUnity.data());
}

// Measure Phase Response for Sinc sampler, no rate conversion.
TEST(Phase, Sinc_Unity) {
  TestUnitySampleRatio(Resampler::WindowedSinc, AudioResult::FreqRespSincUnity.data(),
                       AudioResult::SinadSincUnity.data(), AudioResult::PhaseSincUnity.data());

  EvaluatePhaseResults(AudioResult::PhaseSincUnity.data(), AudioResult::kPrevPhaseSincUnity.data());
}

// Measure Freq Response for Sinc sampler for down-sampling ratio #0.
TEST(FrequencyResponse, Sinc_DownSamp0) {
  TestDownSampleRatio0(Resampler::WindowedSinc, AudioResult::FreqRespSincDown0.data(),
                       AudioResult::SinadSincDown0.data(), AudioResult::PhaseSincDown0.data());

  EvaluateFreqRespResults(AudioResult::FreqRespSincDown0.data(),
                          AudioResult::kPrevFreqRespSincDown0.data());
}

// Measure SINAD for Sinc sampler for down-sampling ratio #0.
TEST(Sinad, Sinc_DownSamp0) {
  TestDownSampleRatio0(Resampler::WindowedSinc, AudioResult::FreqRespSincDown0.data(),
                       AudioResult::SinadSincDown0.data(), AudioResult::PhaseSincDown0.data());

  EvaluateSinadResults(AudioResult::SinadSincDown0.data(), AudioResult::kPrevSinadSincDown0.data());
}

// Measure Out-of-band Rejection for Sinc sampler for down-sampling ratio #0.
TEST(Rejection, Sinc_DownSamp0) {
  TestDownSampleRatio0(Resampler::WindowedSinc, AudioResult::FreqRespSincDown0.data(),
                       AudioResult::SinadSincDown0.data(), AudioResult::PhaseSincDown0.data());

  EvaluateRejectionResults(AudioResult::SinadSincDown0.data(),
                           AudioResult::kPrevSinadSincDown0.data());
}

// Measure Phase Response for Sinc sampler for down-sampling ratio #0.
TEST(Phase, Sinc_DownSamp0) {
  TestDownSampleRatio0(Resampler::WindowedSinc, AudioResult::FreqRespSincDown0.data(),
                       AudioResult::SinadSincDown0.data(), AudioResult::PhaseSincDown0.data());

  EvaluatePhaseResults(AudioResult::PhaseSincDown0.data(), AudioResult::kPrevPhaseSincDown0.data());
}

// Measure Freq Response for Sinc sampler for down-sampling ratio #1.
TEST(FrequencyResponse, Sinc_DownSamp1) {
  TestDownSampleRatio1(Resampler::WindowedSinc, AudioResult::FreqRespSincDown1.data(),
                       AudioResult::SinadSincDown1.data(), AudioResult::PhaseSincDown1.data());

  EvaluateFreqRespResults(AudioResult::FreqRespSincDown1.data(),
                          AudioResult::kPrevFreqRespSincDown1.data());
}

// Measure SINAD for Sinc sampler for down-sampling ratio #1.
TEST(Sinad, Sinc_DownSamp1) {
  TestDownSampleRatio1(Resampler::WindowedSinc, AudioResult::FreqRespSincDown1.data(),
                       AudioResult::SinadSincDown1.data(), AudioResult::PhaseSincDown1.data());

  EvaluateSinadResults(AudioResult::SinadSincDown1.data(), AudioResult::kPrevSinadSincDown1.data());
}

// Measure Out-of-band Rejection for Sinc sampler for down-sampling ratio #1.
TEST(Rejection, Sinc_DownSamp1) {
  TestDownSampleRatio1(Resampler::WindowedSinc, AudioResult::FreqRespSincDown1.data(),
                       AudioResult::SinadSincDown1.data(), AudioResult::PhaseSincDown1.data());

  EvaluateRejectionResults(AudioResult::SinadSincDown1.data(),
                           AudioResult::kPrevSinadSincDown1.data());
}

// Measure Phase Response for Sinc sampler for down-sampling ratio #1.
TEST(Phase, Sinc_DownSamp1) {
  TestDownSampleRatio1(Resampler::WindowedSinc, AudioResult::FreqRespSincDown1.data(),
                       AudioResult::SinadSincDown1.data(), AudioResult::PhaseSincDown1.data());

  EvaluatePhaseResults(AudioResult::PhaseSincDown1.data(), AudioResult::kPrevPhaseSincDown1.data());
}

// Measure Freq Response for Sinc sampler for down-sampling ratio #2.
TEST(FrequencyResponse, Sinc_DownSamp2) {
  TestDownSampleRatio2(Resampler::WindowedSinc, AudioResult::FreqRespSincDown2.data(),
                       AudioResult::SinadSincDown2.data(), AudioResult::PhaseSincDown2.data());

  EvaluateFreqRespResults(AudioResult::FreqRespSincDown2.data(),
                          AudioResult::kPrevFreqRespSincDown2.data());
}

// Measure SINAD for Sinc sampler for down-sampling ratio #2.
TEST(Sinad, Sinc_DownSamp2) {
  TestDownSampleRatio2(Resampler::WindowedSinc, AudioResult::FreqRespSincDown2.data(),
                       AudioResult::SinadSincDown2.data(), AudioResult::PhaseSincDown2.data());

  EvaluateSinadResults(AudioResult::SinadSincDown2.data(), AudioResult::kPrevSinadSincDown2.data());
}

// Measure Out-of-band Rejection for Sinc sampler for down-sampling ratio #2.
TEST(Rejection, Sinc_DownSamp2) {
  TestDownSampleRatio2(Resampler::WindowedSinc, AudioResult::FreqRespSincDown2.data(),
                       AudioResult::SinadSincDown2.data(), AudioResult::PhaseSincDown2.data());

  EvaluateRejectionResults(AudioResult::SinadSincDown2.data(),
                           AudioResult::kPrevSinadSincDown2.data());
}

// Measure Phase Response for Sinc sampler for down-sampling ratio #2.
TEST(Phase, Sinc_DownSamp2) {
  TestDownSampleRatio2(Resampler::WindowedSinc, AudioResult::FreqRespSincDown2.data(),
                       AudioResult::SinadSincDown2.data(), AudioResult::PhaseSincDown2.data());

  EvaluatePhaseResults(AudioResult::PhaseSincDown2.data(), AudioResult::kPrevPhaseSincDown2.data());
}

// Measure Freq Response for Sinc sampler with minimum down-sampling rate change.
TEST(FrequencyResponse, Sinc_MicroSRC) {
  TestMicroSampleRatio(Resampler::WindowedSinc, AudioResult::FreqRespSincMicro.data(),
                       AudioResult::SinadSincMicro.data(), AudioResult::PhaseSincMicro.data());

  EvaluateFreqRespResults(AudioResult::FreqRespSincMicro.data(),
                          AudioResult::kPrevFreqRespSincMicro.data());
}

// Measure SINAD for Sinc sampler with minimum down-sampling rate change.
TEST(Sinad, Sinc_MicroSRC) {
  TestMicroSampleRatio(Resampler::WindowedSinc, AudioResult::FreqRespSincMicro.data(),
                       AudioResult::SinadSincMicro.data(), AudioResult::PhaseSincMicro.data());

  EvaluateSinadResults(AudioResult::SinadSincMicro.data(), AudioResult::kPrevSinadSincMicro.data());
}

// Measure Out-of-band Rejection for Sinc sampler with minimum down-sampling rate change.
TEST(Rejection, Sinc_MicroSRC) {
  TestMicroSampleRatio(Resampler::WindowedSinc, AudioResult::FreqRespSincMicro.data(),
                       AudioResult::SinadSincMicro.data(), AudioResult::PhaseSincMicro.data());

  EvaluateRejectionResults(AudioResult::SinadSincMicro.data(),
                           AudioResult::kPrevSinadSincMicro.data());
}

// Measure Phase Response for Sinc sampler with minimum down-sampling rate change.
TEST(Phase, Sinc_MicroSRC) {
  TestMicroSampleRatio(Resampler::WindowedSinc, AudioResult::FreqRespSincMicro.data(),
                       AudioResult::SinadSincMicro.data(), AudioResult::PhaseSincMicro.data());

  EvaluatePhaseResults(AudioResult::PhaseSincMicro.data(), AudioResult::kPrevPhaseSincMicro.data());
}

// Measure Freq Response for Sinc sampler for up-sampling ratio #1.
TEST(FrequencyResponse, Sinc_UpSamp1) {
  TestUpSampleRatio1(Resampler::WindowedSinc, AudioResult::FreqRespSincUp1.data(),
                     AudioResult::SinadSincUp1.data(), AudioResult::PhaseSincUp1.data());

  EvaluateFreqRespResults(AudioResult::FreqRespSincUp1.data(),
                          AudioResult::kPrevFreqRespSincUp1.data());
}

// Measure SINAD for Sinc sampler for up-sampling ratio #1.
TEST(Sinad, Sinc_UpSamp1) {
  TestUpSampleRatio1(Resampler::WindowedSinc, AudioResult::FreqRespSincUp1.data(),
                     AudioResult::SinadSincUp1.data(), AudioResult::PhaseSincUp1.data());

  EvaluateSinadResults(AudioResult::SinadSincUp1.data(), AudioResult::kPrevSinadSincUp1.data());
}

// Measure Phase Response for Sinc sampler for up-sampling ratio #1.
TEST(Phase, Sinc_UpSamp1) {
  TestUpSampleRatio1(Resampler::WindowedSinc, AudioResult::FreqRespSincUp1.data(),
                     AudioResult::SinadSincUp1.data(), AudioResult::PhaseSincUp1.data());

  EvaluatePhaseResults(AudioResult::PhaseSincUp1.data(), AudioResult::kPrevPhaseSincUp1.data());
}

// Measure Freq Response for Sinc sampler for up-sampling ratio #2.
TEST(FrequencyResponse, Sinc_UpSamp2) {
  TestUpSampleRatio2(Resampler::WindowedSinc, AudioResult::FreqRespSincUp2.data(),
                     AudioResult::SinadSincUp2.data(), AudioResult::PhaseSincUp2.data());

  EvaluateFreqRespResults(AudioResult::FreqRespSincUp2.data(),
                          AudioResult::kPrevFreqRespSincUp2.data());
}

// Measure SINAD for Sinc sampler for up-sampling ratio #2.
TEST(Sinad, Sinc_UpSamp2) {
  TestUpSampleRatio2(Resampler::WindowedSinc, AudioResult::FreqRespSincUp2.data(),
                     AudioResult::SinadSincUp2.data(), AudioResult::PhaseSincUp2.data());

  EvaluateSinadResults(AudioResult::SinadSincUp2.data(), AudioResult::kPrevSinadSincUp2.data());
}

// Measure Phase Response for Sinc sampler for up-sampling ratio #2.
TEST(Phase, Sinc_UpSamp2) {
  TestUpSampleRatio2(Resampler::WindowedSinc, AudioResult::FreqRespSincUp2.data(),
                     AudioResult::SinadSincUp2.data(), AudioResult::PhaseSincUp2.data());

  EvaluatePhaseResults(AudioResult::PhaseSincUp2.data(), AudioResult::kPrevPhaseSincUp2.data());
}

// Measure Freq Response for Sinc sampler for up-sampling ratio #3.
TEST(FrequencyResponse, Sinc_UpSamp3) {
  TestUpSampleRatio3(Resampler::WindowedSinc, AudioResult::FreqRespSincUp3.data(),
                     AudioResult::SinadSincUp3.data(), AudioResult::PhaseSincUp3.data());

  EvaluateFreqRespResults(AudioResult::FreqRespSincUp3.data(),
                          AudioResult::kPrevFreqRespSincUp3.data());
}

// Measure SINAD for Sinc sampler for up-sampling ratio #3.
TEST(Sinad, Sinc_UpSamp3) {
  TestUpSampleRatio3(Resampler::WindowedSinc, AudioResult::FreqRespSincUp3.data(),
                     AudioResult::SinadSincUp3.data(), AudioResult::PhaseSincUp3.data());

  EvaluateSinadResults(AudioResult::SinadSincUp3.data(), AudioResult::kPrevSinadSincUp3.data());
}

// Measure Phase Response for Sinc sampler for up-sampling ratio #3.
TEST(Phase, Sinc_UpSamp3) {
  TestUpSampleRatio3(Resampler::WindowedSinc, AudioResult::FreqRespSincUp3.data(),
                     AudioResult::SinadSincUp3.data(), AudioResult::PhaseSincUp3.data());

  EvaluatePhaseResults(AudioResult::PhaseSincUp3.data(), AudioResult::kPrevPhaseSincUp3.data());
}

// For each summary frequency, populate a sinusoid into a mono buffer, and copy-interleave mono[]
// into one of the channels of the N-channel source.
void PopulateNxNSourceBuffer(float* source, uint32_t num_frames, uint32_t num_chans) {
  auto mono = std::make_unique<float[]>(num_frames);

  // For each summary frequency, populate a sinusoid into mono, and copy-interleave mono into one of
  // the channels of the N-channel source.
  for (uint32_t idx = 0; idx < num_chans; ++idx) {
    uint32_t freq_idx = FrequencySet::kSummaryIdxs[idx];

    // If frequency is too high to be characterized in this buffer length, skip it.
    if (FrequencySet::kReferenceFreqs[freq_idx] * 2 > num_frames) {
      continue;
    }

    // Populate mono[] with a sinusoid at this reference-frequency.
    OverwriteCosine(mono.get(), num_frames, FrequencySet::kReferenceFreqs[freq_idx]);

    // Copy-interleave mono into the N-channel source[].
    for (uint32_t frame_num = 0; frame_num < num_frames; ++frame_num) {
      source[frame_num * num_chans + idx] = mono[frame_num];
    }
  }
}

// For the given resampler, test NxN fidelity equivalence with mono/stereo.
//
// Populate a multi-channel buffer with sinusoids at summary frequencies (one in each channel); mix
// the multi-chan buffer (at micro-SRC); compare each channel to existing mono results.
void TestNxNEquivalence(Resampler sampler_type, double* level_db, double* sinad_db,
                        double* phase_rad) {
  if (!std::isnan(level_db[0])) {
    // This run already has NxN frequency response and SINAD results for this sampler and resampling
    // ratio; don't waste time and cycles rerunning it.
    return;
  }
  // Set this to a valid (worst-case) value, so that (for any outcome) another test does not later
  // rerun this combination of sampler and resample ratio.
  level_db[0] = -INFINITY;

  // For this multi-channel cross-talk test, we put one of the summary frequencies in each channel.
  // We micro-SRC these signals, and ensure that our frequency response, SINAD and phase response
  // are the same as when we test these frequencies in isolation.
  static_assert(FrequencySet::kNumSummaryIdxs <= fuchsia::media::MAX_PCM_CHANNEL_COUNT,
                "Cannot allocate every summary frequency--rework this test.");
  auto num_chans = FrequencySet::kNumSummaryIdxs;
  auto source_rate = 48001;
  auto dest_rate = 48000;
  auto num_src_frames = kFreqTestBufSize + 1;

  // Mix the N-channel source[] into the N-channel accum[].
  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, num_chans, source_rate,
                           num_chans, dest_rate, sampler_type);

  auto num_dest_frames = kFreqTestBufSize;
  // Some resamplers need additional data in order to produce the final values, and the amount of
  // data can change depending on resampling ratio. However, all FFT inputs are considered periodic,
  // so to generate a periodic output from the resampler, we can provide extra source elements to
  // resamplers by simply wrapping around to source[0], etc.
  auto source = std::make_unique<float[]>(num_chans * num_src_frames);
  PopulateNxNSourceBuffer(source.get(), num_src_frames, num_chans);
  auto accum = std::make_unique<float[]>(num_chans * num_dest_frames);

  // We use this to keep ongoing src_pos_modulo across multiple Mix() calls.
  auto& info = mixer->bookkeeping();
  info.step_size = (Mixer::FRAC_ONE * num_src_frames) / num_dest_frames;
  info.rate_modulo = (Mixer::FRAC_ONE * num_src_frames) - (info.step_size * num_dest_frames);
  info.denominator = num_dest_frames;
  info.src_pos_modulo = 0;

  uint32_t dest_frames, dest_offset = 0;
  uint32_t frac_src_frames = num_src_frames * Mixer::FRAC_ONE;

  // First "prime" the resampler by sending a mix command exactly at the end of the source buffer.
  // This allows it to cache the frames at buffer's end. For our testing, buffers are periodic, so
  // these frames are exactly what would have immediately preceded the first data in the buffer.
  // This enables resamplers with significant side width to perform as they would in steady-state.
  int32_t frac_src_offset = static_cast<int32_t>(frac_src_frames);
  auto source_is_consumed = mixer->Mix(accum.get(), num_dest_frames, &dest_offset, source.get(),
                                       frac_src_frames, &frac_src_offset, false);
  FX_CHECK(source_is_consumed);
  FX_CHECK(dest_offset == 0u);
  FX_CHECK(frac_src_offset == static_cast<int32_t>(frac_src_frames));

  // Resample source to accum. (Why in pieces? See kResamplerTestNumPackets in frequency_set.h)
  frac_src_offset = 0;
  for (uint32_t packet = 0; packet < kResamplerTestNumPackets; ++packet) {
    dest_frames = num_dest_frames * (packet + 1) / kResamplerTestNumPackets;
    mixer->Mix(accum.get(), dest_frames, &dest_offset, source.get(), frac_src_frames,
               &frac_src_offset, false);
  }
  int32_t expected_frac_src_offset = frac_src_frames;
  if (dest_offset < dest_frames) {
    AUD_LOG(WARNING) << "Performing wraparound mix: dest_frames " << dest_frames << ", dest_offset "
                     << dest_offset << ", frac_src_frames " << std::hex << frac_src_frames
                     << ", frac_src_offset " << frac_src_offset;
    ASSERT_GE(frac_src_offset, 0);
    EXPECT_GE(static_cast<uint32_t>(frac_src_offset) + mixer->pos_filter_width().raw_value(),
              frac_src_frames)
        << "src_off " << std::hex << frac_src_offset << ", pos_width "
        << mixer->pos_filter_width().raw_value() << ", src_frames " << frac_src_frames;

    // Wrap around in the source buffer -- making the offset slightly negative. We can do
    // this, within the positive filter width of this sampler.
    frac_src_offset -= frac_src_frames;
    mixer->Mix(accum.get(), dest_frames, &dest_offset, source.get(), frac_src_frames,
               &frac_src_offset, false);
    expected_frac_src_offset = 0;
  }
  EXPECT_EQ(dest_offset, dest_frames);
  EXPECT_EQ(frac_src_offset, expected_frac_src_offset);

  // After running each frequency, clear out any remained cached filter state.
  // Currently, this is not strictly necessary since for each frequency test,
  // our initial position is the exact beginning of the buffer (and hence for
  // the Point and Linear resamplers, no previously-cached state is needed).
  // However, this IS a requirement for upcoming resamplers with larger
  // positive filter widths (they exposed the bug; thus addressing it now).
  mixer->Reset();

  // Copy-deinterleave each accum[] channel into mono[] and frequency-analyze.
  auto mono = std::make_unique<float[]>(num_dest_frames);
  for (uint32_t idx = 0; idx < num_chans; ++idx) {
    uint32_t freq_idx = FrequencySet::kSummaryIdxs[idx];

    auto frequency_to_measure = FrequencySet::kReferenceFreqs[freq_idx];
    // If frequency is too high to be characterized in this buffer length, skip it.
    if (frequency_to_measure * 2 >= num_src_frames) {
      if (freq_idx < FrequencySet::kFirstOutBandRefFreqIdx) {
        level_db[freq_idx] = -INFINITY;
        phase_rad[freq_idx] = -INFINITY;
      }
      sinad_db[freq_idx] = -INFINITY;
      continue;
    }

    for (uint32_t i = 0; i < num_dest_frames; ++i) {
      mono[i] = accum[i * num_chans + idx];
    }

    // Copy results to double[], for high-resolution frequency analysis (FFT).
    double magn_signal = -INFINITY, magn_other = INFINITY;
    MeasureAudioFreq(mono.get(), num_dest_frames, frequency_to_measure, &magn_signal, &magn_other,
                     &phase_rad[freq_idx]);

    // Convert Frequency Response and Signal-to-Noise-And-Distortion (SINAD) to decibels.
    if (frequency_to_measure * 2 >= num_dest_frames) {
      // This out-of-band frequency should have been entirely rejected -- capture total magnitude.
      auto magn_total = std::sqrt(magn_signal * magn_signal + magn_other * magn_other);
      sinad_db[freq_idx] = -Gain::DoubleToDb(magn_total);
    } else {
      // This frequency is in-band -- capture its level as well as the magnitude of all else.
      level_db[freq_idx] = Gain::DoubleToDb(magn_signal);
      sinad_db[freq_idx] = Gain::DoubleToDb(magn_signal / magn_other);
    }
  }
}

// Measure Freq Response for NxN Point sampler, with minimum down-sampling rate change.
TEST(FrequencyResponse, Point_NxN) {
  TestNxNEquivalence(Resampler::SampleAndHold, AudioResult::FreqRespPointNxN.data(),
                     AudioResult::SinadPointNxN.data(), AudioResult::PhasePointNxN.data());

  // Final param signals to evaluate only at summary frequencies.
  EvaluateFreqRespResults(AudioResult::FreqRespPointNxN.data(),
                          AudioResult::kPrevFreqRespPointMicro.data(), true);
}

// Measure SINAD for NxN Point sampler, with minimum down-sampling rate change.
TEST(Sinad, Point_NxN) {
  TestNxNEquivalence(Resampler::SampleAndHold, AudioResult::FreqRespPointNxN.data(),
                     AudioResult::SinadPointNxN.data(), AudioResult::PhasePointNxN.data());

  // Final param signals to evaluate only at summary frequencies.
  EvaluateSinadResults(AudioResult::SinadPointNxN.data(), AudioResult::kPrevSinadPointMicro.data(),
                       true);
}

// Measure Phase Response for NxN Point sampler, with minimum down-sampling rate change.
TEST(Phase, Point_NxN) {
  TestNxNEquivalence(Resampler::SampleAndHold, AudioResult::FreqRespPointNxN.data(),
                     AudioResult::SinadPointNxN.data(), AudioResult::PhasePointNxN.data());

  // Final param signals to evaluate only at summary frequencies.
  EvaluatePhaseResults(AudioResult::PhasePointNxN.data(), AudioResult::kPrevPhasePointMicro.data(),
                       true);
}

// Measure Freq Response for NxN Linear sampler, with minimum down-sampling rate change.
TEST(FrequencyResponse, Linear_NxN) {
  TestNxNEquivalence(Resampler::LinearInterpolation, AudioResult::FreqRespLinearNxN.data(),
                     AudioResult::SinadLinearNxN.data(), AudioResult::PhaseLinearNxN.data());

  // Final param signals to evaluate only at summary frequencies.
  EvaluateFreqRespResults(AudioResult::FreqRespLinearNxN.data(),
                          AudioResult::kPrevFreqRespLinearMicro.data(), true);
}

// Measure SINAD for NxN Linear sampler, with minimum down-sampling rate change.
TEST(Sinad, Linear_NxN) {
  TestNxNEquivalence(Resampler::LinearInterpolation, AudioResult::FreqRespLinearNxN.data(),
                     AudioResult::SinadLinearNxN.data(), AudioResult::PhaseLinearNxN.data());

  // Final param signals to evaluate only at summary frequencies.
  EvaluateSinadResults(AudioResult::SinadLinearNxN.data(),
                       AudioResult::kPrevSinadLinearMicro.data(), true);
}

// Measure Phase Response for NxN Linear sampler, with minimum down-sampling rate change.
TEST(Phase, Linear_NxN) {
  TestNxNEquivalence(Resampler::LinearInterpolation, AudioResult::FreqRespLinearNxN.data(),
                     AudioResult::SinadLinearNxN.data(), AudioResult::PhaseLinearNxN.data());

  // Final param signals to evaluate only at summary frequencies.
  EvaluatePhaseResults(AudioResult::PhaseLinearNxN.data(),
                       AudioResult::kPrevPhaseLinearMicro.data(), true);
}

}  // namespace media::audio::test
