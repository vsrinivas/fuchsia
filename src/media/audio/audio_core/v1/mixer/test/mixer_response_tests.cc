// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include <cmath>
#include <iomanip>
#include <ios>

#include "src/media/audio/audio_core/v1/mixer/test/audio_result.h"
#include "src/media/audio/audio_core/v1/mixer/test/frequency_set.h"
#include "src/media/audio/audio_core/v1/mixer/test/mixer_tests_shared.h"
#include "src/media/audio/lib/analysis/generators.h"
#include "src/media/audio/lib/format/constants.h"
#include "src/media/audio/lib/processing/gain.h"
#include "src/media/audio/lib/timeline/timeline_rate.h"

namespace media::audio::test {

// Convenience abbreviation within this source file to shorten names
using Resampler = ::media::audio::Mixer::Resampler;
using ASF = fuchsia::media::AudioSampleFormat;

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
template <ASF SampleFormat>
double MeasureSourceNoiseFloor(double* sinad_db) {
  auto format = Format::Create<SampleFormat>(1, 48000).take_value();
  auto accum_format = Format::Create<ASF::FLOAT>(1, 48000).take_value();

  auto mixer = SelectMixer(SampleFormat, 1, 48000, 1, 48000, Resampler::SampleAndHold);
  if (mixer == nullptr) {
    ADD_FAILURE() << "null mixer";
    return 0.0;
  }

  auto [amplitude, expected_amplitude] = SampleFormatToAmplitudes(SampleFormat);

  // Populate source buffer; mix it (pass-thru) to accumulation buffer
  using SampleT = typename SampleFormatTraits<SampleFormat>::SampleT;
  auto source = GenerateCosineAudio(format, kFreqTestBufSize, FrequencySet::kReferenceFreq,
                                    static_cast<SampleT>(amplitude));
  AudioBuffer accum(accum_format, kFreqTestBufSize);

  int64_t dest_offset = 0;
  auto source_frames = kFreqTestBufSize;
  auto source_offset = Fixed(0);
  mixer->Mix(accum.samples().data(), kFreqTestBufSize, &dest_offset, source.samples().data(),
             source_frames, &source_offset, false);
  EXPECT_EQ(dest_offset, kFreqTestBufSize);
  EXPECT_EQ(source_offset, Fixed(source_frames));

  // Copy result to double-float buffer, FFT (freq-analyze) it at high-res
  auto result = MeasureAudioFreq(AudioBufferSlice(&accum), FrequencySet::kReferenceFreq);

  // Convert Signal-to-Noise-And-Distortion (SINAD) to decibels
  // We can directly compare 'signal' and 'other', regardless of source format.
  *sinad_db = DoubleToDb(result.total_magn_signal / result.total_magn_other);

  // All sources (8-bit, 16-bit, ...) are normalized to float in accum buffer.
  return DoubleToDb(result.total_magn_signal / expected_amplitude);
}

// Measure level response and noise floor for 1kHz sine from 8-bit source.
TEST(NoiseFloor, SourceUint8) {
  AudioResult::LevelSource8 = MeasureSourceNoiseFloor<ASF::UNSIGNED_8>(&AudioResult::FloorSource8);

  EXPECT_NEAR(AudioResult::LevelSource8, 0.0, AudioResult::kPrevLevelToleranceSource8);
  AudioResult::LevelToleranceSource8 =
      fmax(AudioResult::LevelToleranceSource8, abs(AudioResult::LevelSource8));

  EXPECT_GE(AudioResult::FloorSource8, AudioResult::kPrevFloorSource8)
      << std::setprecision(10) << AudioResult::FloorSource8;
}

// Measure level response and noise floor for 1kHz sine from 16-bit source.
TEST(NoiseFloor, SourceInt16) {
  AudioResult::LevelSource16 = MeasureSourceNoiseFloor<ASF::SIGNED_16>(&AudioResult::FloorSource16);

  EXPECT_NEAR(AudioResult::LevelSource16, 0.0, AudioResult::kPrevLevelToleranceSource16);
  AudioResult::LevelToleranceSource16 =
      fmax(AudioResult::LevelToleranceSource16, abs(AudioResult::LevelSource16));

  EXPECT_GE(AudioResult::FloorSource16, AudioResult::kPrevFloorSource16)
      << std::setprecision(10) << AudioResult::FloorSource16;
}

// Measure level response and noise floor for 1kHz sine from 24-bit source.
TEST(NoiseFloor, SourceInt24) {
  AudioResult::LevelSource24 =
      MeasureSourceNoiseFloor<ASF::SIGNED_24_IN_32>(&AudioResult::FloorSource24);

  EXPECT_NEAR(AudioResult::LevelSource24, 0.0, AudioResult::kPrevLevelToleranceSource24);
  AudioResult::LevelToleranceSource24 =
      fmax(AudioResult::LevelToleranceSource24, abs(AudioResult::LevelSource24));

  EXPECT_GE(AudioResult::FloorSource24, AudioResult::kPrevFloorSource24)
      << std::setprecision(10) << AudioResult::FloorSource24;
}

// Measure level response and noise floor for 1kHz sine from float source.
TEST(NoiseFloor, SourceFloat32) {
  AudioResult::LevelSourceFloat =
      MeasureSourceNoiseFloor<ASF::FLOAT>(&AudioResult::FloorSourceFloat);

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
template <ASF SampleFormat>
double MeasureOutputNoiseFloor(double* sinad_db) {
  auto accum_format = Format::Create<ASF::FLOAT>(1, 48000 /* unused */).take_value();
  auto dest_format = Format::Create<SampleFormat>(1, 48000 /* unused */).take_value();

  auto output_producer = OutputProducer::Select(dest_format.stream_type());
  auto [expected_amplitude, amplitude] = SampleFormatToAmplitudes(SampleFormat);

  // Populate accum buffer and output to destination buffer
  auto accum =
      GenerateCosineAudio(accum_format, kFreqTestBufSize, FrequencySet::kReferenceFreq, amplitude);

  AudioBuffer dest(dest_format, kFreqTestBufSize);
  output_producer->ProduceOutput(accum.samples().data(), &dest.samples()[0], kFreqTestBufSize);

  // Copy result to double-float buffer, FFT (freq-analyze) it at high-res
  auto result = MeasureAudioFreq(AudioBufferSlice(&dest), FrequencySet::kReferenceFreq);

  // Convert Signal-to-Noise-And-Distortion (SINAD) to decibels.
  // We can directly compare 'signal' and 'other', regardless of output format.
  *sinad_db = DoubleToDb(result.total_magn_signal / result.total_magn_other);

  return DoubleToDb(result.total_magn_signal / expected_amplitude);
}

// Measure level response and noise floor for 1kHz sine, to an 8-bit output.
TEST(NoiseFloor, OutputUint8) {
  AudioResult::LevelOutput8 = MeasureOutputNoiseFloor<ASF::UNSIGNED_8>(&AudioResult::FloorOutput8);

  EXPECT_NEAR(AudioResult::LevelOutput8, 0.0, AudioResult::kPrevLevelToleranceOutput8);
  AudioResult::LevelToleranceOutput8 =
      fmax(AudioResult::LevelToleranceOutput8, abs(AudioResult::LevelOutput8));

  EXPECT_GE(AudioResult::FloorOutput8, AudioResult::kPrevFloorOutput8)
      << std::setprecision(10) << AudioResult::FloorOutput8;
}

// Measure level response and noise floor for 1kHz sine, to a 16-bit output.
TEST(NoiseFloor, OutputInt16) {
  AudioResult::LevelOutput16 = MeasureOutputNoiseFloor<ASF::SIGNED_16>(&AudioResult::FloorOutput16);

  EXPECT_NEAR(AudioResult::LevelOutput16, 0.0, AudioResult::kPrevLevelToleranceOutput16);
  AudioResult::LevelToleranceOutput16 =
      fmax(AudioResult::LevelToleranceOutput16, abs(AudioResult::LevelOutput16));

  EXPECT_GE(AudioResult::FloorOutput16, AudioResult::kPrevFloorOutput16)
      << std::setprecision(10) << AudioResult::FloorOutput16;
}

// Measure level response and noise floor for 1kHz sine, to a 24-bit output.
TEST(NoiseFloor, OutputInt24) {
  AudioResult::LevelOutput24 =
      MeasureOutputNoiseFloor<ASF::SIGNED_24_IN_32>(&AudioResult::FloorOutput24);

  EXPECT_NEAR(AudioResult::LevelOutput24, 0.0, AudioResult::kPrevLevelToleranceOutput24);
  AudioResult::LevelToleranceOutput24 =
      fmax(AudioResult::LevelToleranceOutput24, abs(AudioResult::LevelOutput24));

  EXPECT_GE(AudioResult::FloorOutput24, AudioResult::kPrevFloorOutput24)
      << std::setprecision(10) << AudioResult::FloorOutput24;
}

// Measure level response and noise floor for 1kHz sine, to a float output.
TEST(NoiseFloor, OutputFloat32) {
  AudioResult::LevelOutputFloat =
      MeasureOutputNoiseFloor<ASF::FLOAT>(&AudioResult::FloorOutputFloat);

  EXPECT_NEAR(AudioResult::LevelOutputFloat, 0.0, AudioResult::kPrevLevelToleranceOutputFloat);
  AudioResult::LevelToleranceOutputFloat =
      fmax(AudioResult::LevelToleranceOutputFloat, abs(AudioResult::LevelOutputFloat));

  EXPECT_GE(AudioResult::FloorOutputFloat, AudioResult::kPrevFloorOutputFloat)
      << std::setprecision(10) << AudioResult::FloorOutputFloat;
}

// Resets mixer state by calling `Mixer::Mix` until all remained filter state gets cleared out.
void ResetMixer(Mixer& mixer, int32_t num_chans) {
  const int64_t frame_count = Fixed(mixer.neg_filter_width() + mixer.pos_filter_width()).Ceiling();
  std::vector<float> zeros(num_chans * frame_count, 0.0f);
  int64_t dest_offset = 0;
  Fixed source_offset = Fixed(0);
  mixer.Mix(zeros.data(), frame_count, &dest_offset, zeros.data(), frame_count, &source_offset,
            false);
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
void MeasureFreqRespSinadPhase(Mixer* mixer, int32_t source_frames, double* level_db,
                               double* sinad_db, double* phase_rad) {
  auto format = Format::Create<ASF::FLOAT>(1, 48000 /* unused */).take_value();

  auto num_dest_frames = kFreqTestBufSize;
  // Some resamplers need additional data in order to produce the final values, and the amount of
  // data can change depending on resampling ratio. However, all FFT inputs are considered periodic,
  // so to generate a periodic output from the resampler, we can provide extra source elements to
  // resamplers by simply wrapping around to source[0], etc.
  AudioBuffer source(format, source_frames);
  AudioBuffer accum(format, num_dest_frames);

  // We use this to keep ongoing source_pos_modulo across multiple Mix() calls.
  auto& state = mixer->state();
  state.ResetSourceStride(TimelineRate(Fixed(source_frames).raw_value(), num_dest_frames));

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
    int32_t freq_idx = idx;
    if (!use_full_set) {
      freq_idx = FrequencySet::kSummaryIdxs[idx];
    }
    auto frequency_to_measure = FrequencySet::kReferenceFreqs[freq_idx];

    // If frequency is too high to be characterized in this buffer, skip it. Per Nyquist limit,
    // buffer length must be at least 2x the frequency we want to measure.
    if (frequency_to_measure * 2 >= source_frames) {
      if (freq_idx < FrequencySet::kFirstOutBandRefFreqIdx) {
        level_db[freq_idx] = -INFINITY;
        phase_rad[freq_idx] = -INFINITY;
      }
      sinad_db[freq_idx] = -INFINITY;
      continue;
    }

    // Populate the source buffer with a sinusoid at each reference frequency.
    source = GenerateCosineAudio(format, source_frames, frequency_to_measure);

    // Start testing each input signal frequency at exactly the same position.
    // Maintain ongoing source_pos_modulo across multiple Mix() calls for that frequency.
    int64_t dest_frames, dest_offset = 0;
    auto source_frames = source.NumFrames();
    state.set_source_pos_modulo(0);

    // First "prime" the resampler by sending a mix command exactly at the end of the source buffer.
    // This allows it to cache the frames at buffer's end. For our testing, buffers are periodic, so
    // these frames are exactly what would have immediately preceded the first data in the buffer.
    // This enables resamplers with significant side width to perform as they would in steady-state.
    auto source_offset = Fixed(source_frames);
    mixer->Mix(accum.samples().data(), num_dest_frames, &dest_offset, source.samples().data(),
               source_frames, &source_offset, false);
    EXPECT_EQ(dest_offset, 0u);
    EXPECT_EQ(source_offset, Fixed(source_frames));

    // Now resample source to accum. (Why in pieces? See kResamplerTestNumPackets: frequency_set.h)
    source_offset = Fixed(0);
    for (uint32_t packet = 0; packet < kResamplerTestNumPackets; ++packet) {
      dest_frames = num_dest_frames * (packet + 1) / kResamplerTestNumPackets;
      mixer->Mix(accum.samples().data(), dest_frames, &dest_offset, source.samples().data(),
                 source_frames, &source_offset, false);
      if (source_offset + mixer->pos_filter_width() >= Fixed(source_frames)) {
        source_offset -= Fixed(source_frames);
      }
    }

    if (dest_offset < dest_frames) {
      auto data = accum.samples();
      FX_LOGS(TRACE) << "Performing wraparound mix: dest_frames " << dest_frames << ", dest_offset "
                     << dest_offset << ", source_frames " << source_frames << ", source_offset 0x"
                     << std::hex << source_offset.raw_value();

      // Wrap around in the source buffer -- making the offset slightly negative. We can do
      // this, within the positive filter width of this sampler.
      mixer->Mix(accum.samples().data(), dest_frames, &dest_offset, source.samples().data(),
                 source_frames, &source_offset, false);
    }
    EXPECT_EQ(dest_offset, dest_frames);
    EXPECT_EQ(source_offset, Fixed(0));

    // After running each frequency, clear the cached filter state. This is not strictly necessary
    // today, since each frequency test starts precisely at buffer-start (thus for Point
    // resamplers, no previously-cached state is needed). However, this IS a requirement for future
    // resamplers with larger positive filter widths (they exposed the bug); address this now.
    ResetMixer(*mixer, /*num_chans=*/1);

    // Is this source frequency beyond the Nyquist limit for our destination frame rate?
    const bool out_of_band = (frequency_to_measure * 2 >= num_dest_frames);
    auto result = out_of_band ? MeasureAudioFreqs(AudioBufferSlice(&accum), {})
                              : MeasureAudioFreqs(AudioBufferSlice(&accum), {frequency_to_measure});

    // Convert Frequency Response and Signal-to-Noise-And-Distortion (SINAD) to decibels.
    if (out_of_band) {
      // This out-of-band frequency should have been entirely rejected -- capture total magnitude.
      // This is equivalent to DoubleToDb(1.0 / result.total_magn_other).
      sinad_db[freq_idx] = -DoubleToDb(result.total_magn_other);
    } else {
      // This frequency is in-band -- capture its level/phase and the magnitude of all else.
      auto magn_signal = result.magnitudes[frequency_to_measure];
      auto magn_other = result.total_magn_other;
      level_db[freq_idx] = DoubleToDb(magn_signal);
      sinad_db[freq_idx] = DoubleToDb(magn_signal / magn_other);
      phase_rad[freq_idx] = result.phases[frequency_to_measure];
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
    int32_t freq_idx = use_full_set ? idx : FrequencySet::kSummaryIdxs[idx];

    EXPECT_GE(freq_resp_results[freq_idx],
              freq_resp_limits[freq_idx] - AudioResult::kFreqRespTolerance)
        << " [" << freq_idx << "]  " << std::fixed << std::setprecision(3)
        << std::floor(freq_resp_results[freq_idx] / AudioResult::kFreqRespTolerance) *
               AudioResult::kFreqRespTolerance;
    EXPECT_LE(freq_resp_results[freq_idx], 0.0 + AudioResult::kPrevLevelToleranceInterpolation)
        << " [" << freq_idx << "]  " << std::scientific << std::setprecision(9)
        << freq_resp_results[freq_idx];
    AudioResult::LevelToleranceInterpolation =
        fmax(AudioResult::LevelToleranceInterpolation, freq_resp_results[freq_idx]);
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
    int32_t freq_idx = use_full_set ? idx : FrequencySet::kSummaryIdxs[idx];

    EXPECT_GE(sinad_results[freq_idx], sinad_limits[freq_idx] - AudioResult::kSinadTolerance)
        << " [" << freq_idx << "]  " << std::fixed << std::setprecision(3)
        << std::floor(sinad_results[freq_idx] / AudioResult::kSinadTolerance) *
               AudioResult::kSinadTolerance;
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

  for (int32_t freq_idx = 0u; freq_idx < FrequencySet::kNumReferenceFreqs; ++freq_idx) {
    if (freq_idx < FrequencySet::kFirstInBandRefFreqIdx ||
        freq_idx >= FrequencySet::kFirstOutBandRefFreqIdx) {
      EXPECT_GE(rejection_results[freq_idx],
                rejection_limits[freq_idx] - AudioResult::kSinadTolerance)
          << " [" << freq_idx << "]  " << std::fixed << std::setprecision(3)
          << std::floor(rejection_results[freq_idx] / AudioResult::kSinadTolerance) *
                 AudioResult::kSinadTolerance;
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
    auto freq_idx = use_full_set ? idx : FrequencySet::kSummaryIdxs[idx];

    if (phase_limits[freq_idx] == -INFINITY) {
      continue;
    }

    if ((phase_results[freq_idx] - phase_limits[freq_idx]) >= M_PI) {
      EXPECT_NEAR(phase_results[freq_idx], phase_limits[freq_idx] + (2.0 * M_PI),
                  AudioResult::kPhaseTolerance)
          << " [" << freq_idx << "]  " << std::fixed << std::setprecision(5)
          << phase_results[freq_idx];
    } else if ((phase_results[freq_idx] - phase_limits[freq_idx]) <= -M_PI) {
      EXPECT_NEAR(phase_results[freq_idx], phase_limits[freq_idx] - (2.0 * M_PI),
                  AudioResult::kPhaseTolerance)
          << " [" << freq_idx << "]  " << std::fixed << std::setprecision(5)
          << phase_results[freq_idx];
    } else {
      EXPECT_NEAR(phase_results[freq_idx], phase_limits[freq_idx], AudioResult::kPhaseTolerance)
          << " [" << freq_idx << "]  " << std::fixed << std::setprecision(5)
          << phase_results[freq_idx];
    }
  }
}

// For the given resampler, measure frequency response and sinad at unity (no SRC), articulated by
// source buffer length equal to dest length.
void TestUnitySampleRatio(Resampler sampler_type, double* freq_resp_results, double* sinad_results,
                          double* phase_results) {
  auto mixer = SelectMixer(ASF::FLOAT, 1, 48000, 1, 48000, sampler_type);
  ASSERT_NE(mixer, nullptr);

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
  auto mixer = SelectMixer(ASF::FLOAT, 1, 191999, 1, 48000, sampler_type);
  ASSERT_NE(mixer, nullptr);

  MeasureFreqRespSinadPhase(mixer.get(), (kFreqTestBufSize << 2) - 1, freq_resp_results,
                            sinad_results, phase_results);
}

// For the given resampler, target a 2:1 downsampling ratio, articulated by specifying a source
// buffer twice the length of the destination buffer.
void TestDownSampleRatio1(Resampler sampler_type, double* freq_resp_results, double* sinad_results,
                          double* phase_results) {
  auto mixer = SelectMixer(ASF::FLOAT, 1, 48000 * 2, 1, 48000, sampler_type);
  ASSERT_NE(mixer, nullptr);

  MeasureFreqRespSinadPhase(mixer.get(), kFreqTestBufSize << 1, freq_resp_results, sinad_results,
                            phase_results);
}

// For the given resampler, target 88200->48000 downsampling, articulated by specifying a source
// buffer longer than destination buffer by that ratio.
void TestDownSampleRatio2(Resampler sampler_type, double* freq_resp_results, double* sinad_results,
                          double* phase_results) {
  auto mixer = SelectMixer(ASF::FLOAT, 1, 88200, 1, 48000, sampler_type);
  ASSERT_NE(mixer, nullptr);

  auto source_frames = std::round(static_cast<double>(kFreqTestBufSize) * 88200.0 / 48000.0);
  MeasureFreqRespSinadPhase(mixer.get(), static_cast<int32_t>(source_frames), freq_resp_results,
                            sinad_results, phase_results);
}

// For the given resampler, target micro-sampling -- with a 48001:48000 ratio.
void TestMicroSampleRatio(Resampler sampler_type, double* freq_resp_results, double* sinad_results,
                          double* phase_results) {
  auto mixer = SelectMixer(ASF::FLOAT, 1, 48001, 1, 48000, sampler_type);
  ASSERT_NE(mixer, nullptr);

  MeasureFreqRespSinadPhase(mixer.get(), kFreqTestBufSize + 1, freq_resp_results, sinad_results,
                            phase_results);
}

// For the given resampler, target 44100->48000 upsampling, articulated by specifying a source
// buffer shorter than destination buffer by that ratio.
void TestUpSampleRatio1(Resampler sampler_type, double* freq_resp_results, double* sinad_results,
                        double* phase_results) {
  auto mixer = SelectMixer(ASF::FLOAT, 1, 44100, 1, 48000, sampler_type);
  ASSERT_NE(mixer, nullptr);

  auto source_frames = std::round(static_cast<double>(kFreqTestBufSize * 44100.0 / 48000.0));
  MeasureFreqRespSinadPhase(mixer.get(), static_cast<int32_t>(source_frames), freq_resp_results,
                            sinad_results, phase_results);
}

// For the given resampler, target the 1:2 upsampling ratio, articulated by specifying a source
// buffer at half the length of the destination buffer.
void TestUpSampleRatio2(Resampler sampler_type, double* freq_resp_results, double* sinad_results,
                        double* phase_results) {
  auto mixer = SelectMixer(ASF::FLOAT, 1, 24000, 1, 24000 * 2, sampler_type);
  ASSERT_NE(mixer, nullptr);

  MeasureFreqRespSinadPhase(mixer.get(), kFreqTestBufSize >> 1, freq_resp_results, sinad_results,
                            phase_results);
}

// For this resampler, target the upsampling ratio "almost 1:4". EXACTLY 1:4 (combined with our
// chosen buffer size, and the system definition of STEP_SIZE), would exceed MAX_INT for source_pos.
// We specify a source buffer at _just_greater__than_ 1/4 the length of the destination buffer.
void TestUpSampleRatio3(Resampler sampler_type, double* freq_resp_results, double* sinad_results,
                        double* phase_results) {
  auto mixer = SelectMixer(ASF::FLOAT, 1, 12001, 1, 48000, sampler_type);
  ASSERT_NE(mixer, nullptr);

  MeasureFreqRespSinadPhase(mixer.get(), (kFreqTestBufSize >> 2) + 1, freq_resp_results,
                            sinad_results, phase_results);
}

// Measure Freq Response, SINAD and Phase Response for Point sampler, no rate conversion.
TEST(PointUnity, TotalResponse) {
  TestUnitySampleRatio(Resampler::SampleAndHold, AudioResult::FreqRespPointUnity.data(),
                       AudioResult::SinadPointUnity.data(), AudioResult::PhasePointUnity.data());

  EvaluateFreqRespResults(AudioResult::FreqRespPointUnity.data(),
                          AudioResult::kPrevFreqRespUnity.data());

  EvaluateSinadResults(AudioResult::SinadPointUnity.data(), AudioResult::kPrevSinadUnity.data());

  EvaluatePhaseResults(AudioResult::PhasePointUnity.data(), AudioResult::kPrevPhaseUnity.data());
}

// Measure Freq Response, SINAD and Phase Response for Sinc sampler, no rate conversion.
TEST(SincUnity, TotalResponse) {
  TestUnitySampleRatio(Resampler::WindowedSinc, AudioResult::FreqRespSincUnity.data(),
                       AudioResult::SinadSincUnity.data(), AudioResult::PhaseSincUnity.data());

  EvaluateFreqRespResults(AudioResult::FreqRespSincUnity.data(),
                          AudioResult::kPrevFreqRespUnity.data());

  EvaluateSinadResults(AudioResult::SinadSincUnity.data(), AudioResult::kPrevSinadUnity.data());

  EvaluatePhaseResults(AudioResult::PhaseSincUnity.data(), AudioResult::kPrevPhaseUnity.data());
}

// Measure Freq Response, SINAD, Out-of-band Rejection and Phase Response for Sinc sampler for
// down-sampling ratio #0.
TEST(SincDownSamp0, TotalResponse) {
  TestDownSampleRatio0(Resampler::WindowedSinc, AudioResult::FreqRespSincDown0.data(),
                       AudioResult::SinadSincDown0.data(), AudioResult::PhaseSincDown0.data());

  EvaluateFreqRespResults(AudioResult::FreqRespSincDown0.data(),
                          AudioResult::kPrevFreqRespSincDown0.data());

  EvaluateSinadResults(AudioResult::SinadSincDown0.data(), AudioResult::kPrevSinadSincDown0.data());

  EvaluateRejectionResults(AudioResult::SinadSincDown0.data(),
                           AudioResult::kPrevSinadSincDown0.data());

  EvaluatePhaseResults(AudioResult::PhaseSincDown0.data(), AudioResult::kPrevPhaseSincDown0.data());
}

// Measure Freq Response, SINAD, Out-of-band Rejection and Phase Response for Sinc sampler for
// down-sampling ratio #1.
TEST(SincDownSamp1, TotalResponse) {
  TestDownSampleRatio1(Resampler::WindowedSinc, AudioResult::FreqRespSincDown1.data(),
                       AudioResult::SinadSincDown1.data(), AudioResult::PhaseSincDown1.data());

  EvaluateFreqRespResults(AudioResult::FreqRespSincDown1.data(),
                          AudioResult::kPrevFreqRespSincDown1.data());

  EvaluateSinadResults(AudioResult::SinadSincDown1.data(), AudioResult::kPrevSinadSincDown1.data());

  EvaluateRejectionResults(AudioResult::SinadSincDown1.data(),
                           AudioResult::kPrevSinadSincDown1.data());

  EvaluatePhaseResults(AudioResult::PhaseSincDown1.data(), AudioResult::kPrevPhaseSincDown1.data());
}

// Measure Freq Response, SINAD, Out-of-band Rejection and Phase Response for Sinc sampler for
// down-sampling ratio #2.
TEST(SincDownSamp2, TotalResponse) {
  TestDownSampleRatio2(Resampler::WindowedSinc, AudioResult::FreqRespSincDown2.data(),
                       AudioResult::SinadSincDown2.data(), AudioResult::PhaseSincDown2.data());

  EvaluateFreqRespResults(AudioResult::FreqRespSincDown2.data(),
                          AudioResult::kPrevFreqRespSincDown2.data());

  EvaluateSinadResults(AudioResult::SinadSincDown2.data(), AudioResult::kPrevSinadSincDown2.data());

  EvaluateRejectionResults(AudioResult::SinadSincDown2.data(),
                           AudioResult::kPrevSinadSincDown2.data());

  EvaluatePhaseResults(AudioResult::PhaseSincDown2.data(), AudioResult::kPrevPhaseSincDown2.data());
}

// Measure Freq Response, SINAD, Out-of-band Rejection and Phase Response for Sinc sampler with
// minimum down-sampling rate change.
TEST(SincMicroSRC, TotalResponse) {
  TestMicroSampleRatio(Resampler::WindowedSinc, AudioResult::FreqRespSincMicro.data(),
                       AudioResult::SinadSincMicro.data(), AudioResult::PhaseSincMicro.data());

  EvaluateFreqRespResults(AudioResult::FreqRespSincMicro.data(),
                          AudioResult::kPrevFreqRespSincMicro.data());

  EvaluateSinadResults(AudioResult::SinadSincMicro.data(), AudioResult::kPrevSinadSincMicro.data());

  EvaluateRejectionResults(AudioResult::SinadSincMicro.data(),
                           AudioResult::kPrevSinadSincMicro.data());

  EvaluatePhaseResults(AudioResult::PhaseSincMicro.data(), AudioResult::kPrevPhaseSincMicro.data());
}

// Measure Freq Response, SINAD and Phase Response for Sinc sampler for up-sampling ratio #1.
TEST(SincUpSamp1, TotalResponse) {
  TestUpSampleRatio1(Resampler::WindowedSinc, AudioResult::FreqRespSincUp1.data(),
                     AudioResult::SinadSincUp1.data(), AudioResult::PhaseSincUp1.data());

  EvaluateFreqRespResults(AudioResult::FreqRespSincUp1.data(),
                          AudioResult::kPrevFreqRespSincUp1.data());

  EvaluateSinadResults(AudioResult::SinadSincUp1.data(), AudioResult::kPrevSinadSincUp1.data());

  EvaluatePhaseResults(AudioResult::PhaseSincUp1.data(), AudioResult::kPrevPhaseSincUp1.data());
}

// Measure Freq Response, SINAD and Phase Response for Sinc sampler for up-sampling ratio #2.
TEST(SincUpSamp2, TotalResponse) {
  TestUpSampleRatio2(Resampler::WindowedSinc, AudioResult::FreqRespSincUp2.data(),
                     AudioResult::SinadSincUp2.data(), AudioResult::PhaseSincUp2.data());

  EvaluateFreqRespResults(AudioResult::FreqRespSincUp2.data(),
                          AudioResult::kPrevFreqRespSincUp2.data());

  EvaluateSinadResults(AudioResult::SinadSincUp2.data(), AudioResult::kPrevSinadSincUp2.data());

  EvaluatePhaseResults(AudioResult::PhaseSincUp2.data(), AudioResult::kPrevPhaseSincUp2.data());
}

// Measure Freq Response, SINAD and Phase Response for Sinc sampler for up-sampling ratio #3.
TEST(SincUpSamp3, TotalResponse) {
  TestUpSampleRatio3(Resampler::WindowedSinc, AudioResult::FreqRespSincUp3.data(),
                     AudioResult::SinadSincUp3.data(), AudioResult::PhaseSincUp3.data());

  EvaluateFreqRespResults(AudioResult::FreqRespSincUp3.data(),
                          AudioResult::kPrevFreqRespSincUp3.data());

  EvaluateSinadResults(AudioResult::SinadSincUp3.data(), AudioResult::kPrevSinadSincUp3.data());

  EvaluatePhaseResults(AudioResult::PhaseSincUp3.data(), AudioResult::kPrevPhaseSincUp3.data());
}

// For each summary frequency, populate a sinusoid into a mono buffer, and copy-interleave mono[]
// into one of the channels of the N-channel source.
AudioBuffer<ASF::FLOAT> PopulateNxNSourceBuffer(int64_t num_frames, int32_t num_chans,
                                                int32_t rate) {
  auto format = Format::Create<ASF::FLOAT>(num_chans, rate).take_value();
  auto source = AudioBuffer(format, num_frames);

  // For each summary frequency, populate a sinusoid into mono, and copy-interleave mono into one of
  // the channels of the N-channel source.
  for (auto idx = 0; idx < num_chans; ++idx) {
    int32_t freq_idx = FrequencySet::kSummaryIdxs[idx];

    // If frequency is too high to be characterized in this buffer length, skip it.
    if (FrequencySet::kReferenceFreqs[freq_idx] * 2 > num_frames) {
      continue;
    }

    // Populate mono[] with a sinusoid at this reference-frequency.
    auto format = Format::Create<ASF::FLOAT>(1, 48000 /* unused */).take_value();
    auto mono = GenerateCosineAudio(format, num_frames, FrequencySet::kReferenceFreqs[freq_idx]);

    // Copy-interleave mono into the N-channel source[].
    for (int32_t frame_num = 0; frame_num < num_frames; ++frame_num) {
      source.samples()[frame_num * num_chans + idx] = mono.samples()[frame_num];
    }
  }

  return source;
}

// For the given resampler, test NxN fidelity equivalence with mono/stereo.
//
// Populate a multi-channel buffer with sinusoids at summary frequencies (one in each channel); mix
// the multi-chan buffer (at micro-SRC); compare each channel to existing mono results.
void TestNxNEquivalence(Resampler sampler_type, double* level_db, double* sinad_db,
                        double* phase_rad) {
  // For this multi-channel cross-talk test, we put one of the summary frequencies in each channel.
  // We micro-SRC these signals, and ensure that our frequency response, SINAD and phase response
  // are the same as when we test these frequencies in isolation.
  static_assert(FrequencySet::kNumSummaryIdxs <= fuchsia::media::MAX_PCM_CHANNEL_COUNT,
                "Cannot allocate every summary frequency--rework this test.");
  auto num_chans = FrequencySet::kNumSummaryIdxs;
  auto source_rate = 48001u;
  auto dest_rate = 48000u;
  auto source_frames = kFreqTestBufSize + 1;

  // Mix the N-channel source[] into the N-channel accum[].
  auto mixer = SelectMixer(ASF::FLOAT, num_chans, source_rate, num_chans, dest_rate, sampler_type);
  ASSERT_NE(mixer, nullptr);

  auto num_dest_frames = kFreqTestBufSize;
  auto dest_format = Format::Create<ASF::FLOAT>(num_chans, dest_rate).take_value();

  // Some resamplers need additional data in order to produce the final values, and the amount of
  // data can change depending on resampling ratio. However, all FFT inputs are considered periodic,
  // so to generate a periodic output from the resampler, we can provide extra source elements to
  // resamplers by simply wrapping around to source[0], etc.
  auto source = PopulateNxNSourceBuffer(source_frames, num_chans, source_rate);
  auto accum = AudioBuffer(dest_format, num_dest_frames);

  // We use this to keep ongoing source_pos_modulo across multiple Mix() calls.
  mixer->state().ResetSourceStride(TimelineRate(Fixed(source_frames).raw_value(), num_dest_frames));

  int64_t dest_frames, dest_offset = 0;

  // First "prime" the resampler by sending a mix command exactly at the end of the source buffer.
  // This allows it to cache the frames at buffer's end. For our testing, buffers are periodic, so
  // these frames are exactly what would have immediately preceded the first data in the buffer.
  // This enables resamplers with significant side width to perform as they would in steady-state.

  auto source_offset = Fixed(source_frames);
  mixer->Mix(accum.samples().data(), num_dest_frames, &dest_offset, source.samples().data(),
             source_frames, &source_offset, false);
  EXPECT_EQ(dest_offset, 0u);
  EXPECT_EQ(source_offset, Fixed(source_frames));

  // Resample source to accum. (Why in pieces? See kResamplerTestNumPackets in frequency_set.h)
  source_offset = Fixed(0);
  for (uint32_t packet = 0; packet < kResamplerTestNumPackets; ++packet) {
    dest_frames = num_dest_frames * (packet + 1) / kResamplerTestNumPackets;
    mixer->Mix(accum.samples().data(), dest_frames, &dest_offset, source.samples().data(),
               source_frames, &source_offset, false);
  }
  auto expected_source_offset = Fixed(source_frames);
  if (dest_offset < dest_frames) {
    // This is expected, for resamplers with width.
    FX_LOGS(TRACE) << "Performing wraparound mix: dest_frames " << dest_frames << ", dest_offset "
                   << dest_offset << ", source_frames " << source_frames << ", source_offset "
                   << std::hex << source_offset.raw_value();
    ASSERT_GE(source_offset, 0);
    EXPECT_GE(source_offset + mixer->pos_filter_width(), Fixed(source_frames))
        << "source_offset 0x" << std::hex << source_offset.raw_value() << ", pos_width 0x"
        << mixer->pos_filter_width().raw_value() << ", source_frames " << std::dec << source_frames;

    // Wrap around in the source buffer -- making the offset slightly negative. We can do
    // this, within the positive filter width of this sampler.
    source_offset -= Fixed(source_frames);
    mixer->Mix(accum.samples().data(), dest_frames, &dest_offset, source.samples().data(),
               source_frames, &source_offset, false);
    expected_source_offset = Fixed(0);
  }
  EXPECT_EQ(dest_offset, dest_frames);
  EXPECT_EQ(source_offset, expected_source_offset);

  // After running each frequency, clear out any remained cached filter state.
  // Currently, this is not strictly necessary since for each frequency test,
  // our initial position is the exact beginning of the buffer (and hence for
  // the Point resamplers, no previously-cached state is needed).
  // However, this IS a requirement for upcoming resamplers with larger
  // positive filter widths (they exposed the bug; thus addressing it now).
  ResetMixer(*mixer, num_chans);

  auto mono_format = Format::Create<ASF::FLOAT>(1, dest_rate).take_value();
  auto mono = AudioBuffer(mono_format, num_dest_frames);

  // Copy-deinterleave each accum[] channel into mono[] and frequency-analyze.
  for (auto idx = 0; idx < num_chans; ++idx) {
    auto freq_idx = FrequencySet::kSummaryIdxs[idx];

    auto frequency_to_measure = FrequencySet::kReferenceFreqs[freq_idx];
    // If frequency is too high to be characterized in this buffer length, skip it.
    if (frequency_to_measure * 2 >= source_frames) {
      if (freq_idx < FrequencySet::kFirstOutBandRefFreqIdx) {
        level_db[freq_idx] = -INFINITY;
        phase_rad[freq_idx] = -INFINITY;
      }
      sinad_db[freq_idx] = -INFINITY;
      continue;
    }

    for (auto i = 0; i < num_dest_frames; ++i) {
      mono.samples()[i] = accum.samples()[i * num_chans + idx];
    }

    // Is this source frequency beyond the Nyquist limit for our destination frame rate?
    const bool out_of_band = (frequency_to_measure * 2 >= num_dest_frames);
    auto result = out_of_band ? MeasureAudioFreqs(AudioBufferSlice(&mono), {})
                              : MeasureAudioFreqs(AudioBufferSlice(&mono), {frequency_to_measure});

    // Convert Frequency Response and Signal-to-Noise-And-Distortion (SINAD) to decibels.
    if (out_of_band) {
      // This out-of-band frequency should have been entirely rejected -- capture total magnitude.
      // This is equivalent to DoubleToDb(1.0 / result.total_magn_other).
      sinad_db[freq_idx] = -DoubleToDb(result.total_magn_other);
    } else {
      // This frequency is in-band -- capture its level/phase and the magnitude of all else.
      auto magn_signal = result.magnitudes[frequency_to_measure];
      auto magn_other = result.total_magn_other;
      level_db[freq_idx] = DoubleToDb(magn_signal);
      sinad_db[freq_idx] = DoubleToDb(magn_signal / magn_other);
      phase_rad[freq_idx] = result.phases[frequency_to_measure];
    }
  }
}

// Measure Freq Response, SINAD and Phase Response for NxN Sinc sampler, with minimum down-sampling
// rate change. This test case simultaneously evaluates the summary frequencies -- one per channel.
TEST(SincNxN, TotalResponse) {
  TestNxNEquivalence(Resampler::WindowedSinc, AudioResult::FreqRespSincNxN.data(),
                     AudioResult::SinadSincNxN.data(), AudioResult::PhaseSincNxN.data());

  // The final 'true' param signals to evaluate only at summary frequencies.
  EvaluateFreqRespResults(AudioResult::FreqRespSincNxN.data(),
                          AudioResult::kPrevFreqRespSincMicro.data(), true);

  EvaluateSinadResults(AudioResult::SinadSincNxN.data(), AudioResult::kPrevSinadSincMicro.data(),
                       true);

  EvaluatePhaseResults(AudioResult::PhaseSincNxN.data(), AudioResult::kPrevPhaseSincMicro.data(),
                       true);
}

}  // namespace media::audio::test
