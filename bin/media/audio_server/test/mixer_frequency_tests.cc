// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/test/audio_result.h"
#include "garnet/bin/media/audio_server/test/mixer_tests_shared.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {
namespace test {

//
// Pass-thru Noise-Floor tests
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

  if (std::is_same<T, uint8_t>::value) {
    mixer = SelectMixer(AudioSampleFormat::UNSIGNED_8, 1, 48000, 1, 48000);
  } else if (std::is_same<T, int16_t>::value) {
    mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000);
  } else {
    FXL_DCHECK(false) << "Unsupported source format";
  }

  const double amplitude = (std::is_same<T, uint8_t>::value)
                               ? std::numeric_limits<int8_t>::max()
                               : std::numeric_limits<int16_t>::max();

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

  // All sources (8-bit, 16-bit, ...) are normalized to int16 in accum buffer.
  return ValToDb(magn_signal / std::numeric_limits<int16_t>::max());
}

// Measure level response and noise floor for 1kHz sine from 8-bit source.
TEST(NoiseFloor, Source_8) {
  double level_db =
      MeasureSourceNoiseFloor<uint8_t>(&AudioResult::FloorSource8);

  EXPECT_GE(level_db, -AudioResult::kLevelToleranceSource8);
  EXPECT_LE(level_db, AudioResult::kLevelToleranceSource8);

  EXPECT_GE(AudioResult::FloorSource8, AudioResult::kPrevFloorSource8);
}

// Measure level response and noise floor for 1kHz sine from 16bit source.
TEST(NoiseFloor, Source_16) {
  double level_db =
      MeasureSourceNoiseFloor<int16_t>(&AudioResult::FloorSource16);

  EXPECT_GE(level_db, -AudioResult::kLevelToleranceSource16);
  EXPECT_LE(level_db, AudioResult::kLevelToleranceSource16);

  EXPECT_GE(AudioResult::FloorSource16, AudioResult::kPrevFloorSource16);
}

template <typename T>
double MeasureOutputNoiseFloor(double* sinad_db) {
  OutputFormatterPtr output_formatter;

  if (std::is_same<T, uint8_t>::value) {
    output_formatter = SelectOutputFormatter(AudioSampleFormat::UNSIGNED_8, 1);
  } else if (std::is_same<T, int16_t>::value) {
    output_formatter = SelectOutputFormatter(AudioSampleFormat::SIGNED_16, 1);
  } else {
    FXL_DCHECK(false) << "Unsupported source format";
  }

  // Populate accum buffer and output to destination buffer
  std::vector<int32_t> accum(kFreqTestBufSize);
  OverwriteCosine(accum.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq,
                  std::numeric_limits<int16_t>::max());

  std::vector<T> dest(kFreqTestBufSize);
  output_formatter->ProduceOutput(accum.data(), dest.data(), kFreqTestBufSize);

  // Copy result to double-float buffer, FFT (freq-analyze) it at high-res
  double magn_signal, magn_other;
  MeasureAudioFreq(dest.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq,
                   &magn_signal, &magn_other);

  // Calculate Signal-to-Noise-And-Distortion (SINAD)
  // We can directly compare 'signal' and 'other', regardless of output format.
  *sinad_db = ValToDb(magn_signal / magn_other);

  // Calculate magnitude of primary signal strength
  // For 8-bit output, compensate for the shift it got on the way to accum.
  // N.B.: using int8::max (not uint8::max is intentional, as within uint8
  // we still use a maximum amplitude of 127 (it is just centered on 128).
  if (std::is_same<T, uint8_t>::value) {
    return ValToDb(magn_signal / std::numeric_limits<int8_t>::max());
  } else {
    return ValToDb(magn_signal / std::numeric_limits<int16_t>::max());
  }
}

// Measure level response and noise floor for 1kHz sine, to an 8bit output.
TEST(NoiseFloor, Output_8) {
  double level_db =
      MeasureOutputNoiseFloor<uint8_t>(&AudioResult::FloorOutput8);

  EXPECT_GE(level_db, -AudioResult::kLevelToleranceOutput8);
  EXPECT_LE(level_db, AudioResult::kLevelToleranceOutput8);

  EXPECT_GE(AudioResult::FloorOutput8, AudioResult::kPrevFloorOutput8);
}

// Measure level response and noise floor for 1kHz sine, to a 16bit output.
TEST(NoiseFloor, Output_16) {
  double level_db =
      MeasureOutputNoiseFloor<int16_t>(&AudioResult::FloorOutput16);

  EXPECT_GE(level_db, -AudioResult::kLevelToleranceOutput16);
  EXPECT_LE(level_db, AudioResult::kLevelToleranceOutput16);

  EXPECT_GE(AudioResult::FloorOutput16, AudioResult::kPrevFloorOutput16);
}

// Ideal frequency response measurement is 0.00 dB across the audible spectrum
// Ideal SINAD is at least 6 dB per signal-bit (which here is 16, so >96 dB).
void MeasureSummaryFreqRespSinad(MixerPtr mixer,
                                 uint32_t step_size,
                                 double* level_db,
                                 double* sinad_db) {
  const uint32_t src_buf_size =
      step_size * (kFreqTestBufSize >> kPtsFractionalBits);

  // Source has extra val: linear interp needs it to calc the final dest val.
  std::vector<int16_t> source(src_buf_size + 1);
  std::vector<int32_t> accum(kFreqTestBufSize);

  // Measure frequency reseponse for each summary frequency
  for (uint32_t freq = 0; freq < FrequencySet::kNumSummaryFreqs; ++freq) {
    // Populate source buffer; mix it (pass-thru) to accumulation buffer
    OverwriteCosine(source.data(), src_buf_size,
                    FrequencySet::kSummaryFreqs[freq],
                    std::numeric_limits<int16_t>::max());
    source[src_buf_size] = source[0];

    uint32_t dst_offset = 0;
    int32_t frac_src_offset = 0;
    mixer->Mix(accum.data(), kFreqTestBufSize, &dst_offset, source.data(),
               (src_buf_size + 1) << kPtsFractionalBits, &frac_src_offset,
               step_size, Gain::kUnityScale, false);
    EXPECT_EQ(kFreqTestBufSize, dst_offset);
    EXPECT_EQ(static_cast<int32_t>(src_buf_size << kPtsFractionalBits),
              frac_src_offset);

    // Copy result to double-float buffer, FFT (freq-analyze) it at high-res
    double magn_signal, magn_other;
    MeasureAudioFreq(accum.data(), kFreqTestBufSize,
                     FrequencySet::kSummaryFreqs[freq], &magn_signal,
                     &magn_other);

    if (FrequencySet::kSummaryFreqs[freq] == FrequencySet::kReferenceFreq) {
      // Calculate Signal-to-Noise-And-Distortion (SINAD)
      *sinad_db = ValToDb(magn_signal / magn_other);
    }

    level_db[freq] = ValToDb(magn_signal / std::numeric_limits<int16_t>::max());
  }
}

// Measure summary Freq Response & SINAD for Point sampler, no rate conversion.
TEST(FrequencyResponse, Point_Unity) {
  MixerPtr mixer =
      SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000);
  constexpr uint32_t step_size = Mixer::FRAC_ONE;  // 48k->48k

  MeasureSummaryFreqRespSinad(std::move(mixer), step_size,
                              AudioResult::FreqRespPointUnity,
                              &AudioResult::SinadPointUnity);

  for (uint32_t freq = 0; freq < FrequencySet::kNumSummaryFreqs; ++freq) {
    EXPECT_GE(AudioResult::FreqRespPointUnity[freq],
              AudioResult::kPrevFreqRespPointUnity[freq])
        << freq;
    EXPECT_LE(AudioResult::FreqRespPointUnity[freq],
              0.0 + AudioResult::kLevelToleranceSource16)
        << freq;
  }

  EXPECT_GE(AudioResult::SinadPointUnity, AudioResult::kPrevSinadPointUnity);
}

// Measure summary Freq Response & SINAD for Point sampler, down-sampling.
TEST(FrequencyResponse, Point_DownSamp) {
  MixerPtr mixer =
      SelectMixer(AudioSampleFormat::SIGNED_16, 1, 96000, 1, 48000);
  constexpr uint32_t step_size = Mixer::FRAC_ONE << 1;  // 96k -> 48k

  MeasureSummaryFreqRespSinad(std::move(mixer), step_size,
                              AudioResult::FreqRespPointDown,
                              &AudioResult::SinadPointDown);

  for (uint32_t freq = 0; freq < FrequencySet::kNumSummaryFreqs; ++freq) {
    EXPECT_GE(AudioResult::FreqRespPointDown[freq],
              AudioResult::kPrevFreqRespPointDown[freq])
        << freq;
    EXPECT_LE(AudioResult::FreqRespPointDown[freq],
              0.0 + AudioResult::kLevelToleranceSource16)
        << freq;
  }

  EXPECT_GE(AudioResult::SinadPointDown, AudioResult::kPrevSinadPointDown);
}

// Measure summary Freq Response & SINAD for Linear sampler, down-sampling.
TEST(FrequencyResponse, Linear_DownSamp) {
  MixerPtr mixer =
      SelectMixer(AudioSampleFormat::SIGNED_16, 1, 88200, 1, 48000);
  constexpr uint32_t step_size = 0x1D67;  // 88.2k -> 48k

  MeasureSummaryFreqRespSinad(std::move(mixer), step_size,
                              AudioResult::FreqRespLinearDown,
                              &AudioResult::SinadLinearDown);

  for (uint32_t freq = 0; freq < FrequencySet::kNumSummaryFreqs; ++freq) {
    EXPECT_GE(AudioResult::FreqRespLinearDown[freq],
              AudioResult::kPrevFreqRespLinearDown[freq])
        << freq;
    EXPECT_LE(AudioResult::FreqRespLinearDown[freq],
              0.0 + AudioResult::kLevelToleranceSource16)
        << freq;
  }
  EXPECT_GE(AudioResult::SinadLinearDown, AudioResult::kPrevSinadLinearDown);
}

// Measure summary Freq Response & SINAD for Linear sampler, up-sampling.
TEST(FrequencyResponse, Linear_UpSamp) {
  MixerPtr mixer =
      SelectMixer(AudioSampleFormat::SIGNED_16, 1, 44100, 1, 48000);
  constexpr uint32_t step_size = 0x0EB3;  // 44.1k -> 48k

  MeasureSummaryFreqRespSinad(std::move(mixer), step_size,
                              AudioResult::FreqRespLinearUp,
                              &AudioResult::SinadLinearUp);

  for (uint32_t freq = 0; freq < FrequencySet::kNumSummaryFreqs; ++freq) {
    EXPECT_GE(AudioResult::FreqRespLinearUp[freq],
              AudioResult::kPrevFreqRespLinearUp[freq])
        << freq;
    EXPECT_LE(AudioResult::FreqRespLinearUp[freq],
              0.0 + AudioResult::kLevelToleranceSource16)
        << freq;
  }
  EXPECT_GE(AudioResult::SinadLinearUp, AudioResult::kPrevSinadLinearUp);
}

// Ideal dynamic range measurement is exactly equal to the reduction in gain.
// Ideal accompanying noise is ideal noise floor, minus the reduction in gain.
void MeasureSummaryDynamicRange(Gain::AScale scale,
                                double* level_db,
                                double* sinad_db) {
  MixerPtr mixer =
      SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000);
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
// For now (until MTWN-73 is fixed) these increments are actually the same.
TEST(DynamicRange, Epsilon) {
  double unity_level_db, unity_sinad_db;

  MeasureSummaryDynamicRange(Gain::kUnityScale, &unity_level_db,
                             &unity_sinad_db);
  EXPECT_GE(unity_level_db, -AudioResult::kLevelToleranceSource16);
  EXPECT_LE(unity_level_db, AudioResult::kLevelToleranceSource16);
  EXPECT_GE(unity_sinad_db, AudioResult::kPrevFloorSource16);

  // Highest (nearest 1.0) gain_scale at which we observe an effect on signals
  constexpr Gain::AScale epsilon_scale = Gain::kUnityScale - 1;

  // At this 'detectable reduction' scale, level and noise floor appear reduced
  MeasureSummaryDynamicRange(epsilon_scale, &AudioResult::LevelDownEpsilon,
                             &AudioResult::SinadDownEpsilon);
  EXPECT_GE(
      AudioResult::LevelDownEpsilon,
      AudioResult::kPrevLevelDownEpsilon - AudioResult::kPrevDynRangeTolerance);
  EXPECT_LE(
      AudioResult::LevelDownEpsilon,
      AudioResult::kPrevLevelDownEpsilon + AudioResult::kPrevDynRangeTolerance);
  EXPECT_LT(AudioResult::LevelDownEpsilon, unity_level_db);

  EXPECT_GE(AudioResult::SinadDownEpsilon, AudioResult::kPrevSinadDownEpsilon);
}

// Measure dynamic range (signal level, noise floor) when gain is -60dB.
TEST(DynamicRange, Down60) {
  Gain gain;

  gain.SetRendererGain(-60.0f);
  const Gain::AScale scale = gain.GetGainScale(0.0f);

  MeasureSummaryDynamicRange(scale, &AudioResult::LevelDown60,
                             &AudioResult::SinadDown60);

  EXPECT_GE(AudioResult::LevelDown60,
            -60.0 - AudioResult::kPrevDynRangeTolerance);
  EXPECT_LE(AudioResult::LevelDown60,
            -60.0 + AudioResult::kPrevDynRangeTolerance);
  EXPECT_GE(AudioResult::SinadDown60, AudioResult::kPrevSinadDown60);

  // Validate level & floor in equivalent gain combination (per-stream, master).
  gain.SetRendererGain(0.0f);
  const Gain::AScale scale2 = gain.GetGainScale(-60.0f);

  double level_db, sinad_db;
  MeasureSummaryDynamicRange(scale2, &level_db, &sinad_db);

  EXPECT_EQ(level_db, AudioResult::LevelDown60);
  EXPECT_EQ(sinad_db, AudioResult::SinadDown60);
}
}  // namespace test
}  // namespace audio
}  // namespace media
