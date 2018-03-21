// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/test/audio_result.h"
#include "garnet/bin/media/audio_server/test/mixer_tests_shared.h"
#include "lib/fxl/logging.h"

namespace media {
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
double MeasureSourceNoiseFloor(double* magn_other_db) {
  audio::MixerPtr mixer;

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
             kFreqTestBufSize << audio::kPtsFractionalBits, &frac_src_offset,
             media::audio::Mixer::FRAC_ONE, audio::Gain::kUnityScale, false);
  EXPECT_EQ(kFreqTestBufSize, dst_offset);
  EXPECT_EQ(static_cast<int32_t>(kFreqTestBufSize << audio::kPtsFractionalBits),
            frac_src_offset);

  // Copy result to double-float buffer, FFT (freq-analyze) it at high-res
  double magn_signal, magn_other;
  MeasureAudioFreq(accum.data(), kFreqTestBufSize, FrequencySet::kReferenceFreq,
                   &magn_signal, &magn_other);

  // Calculate Signal-to-Noise-And-Distortion (SINAD)
  // We can directly compare 'signal' and 'other', regardless of source format.
  *magn_other_db = ValToDb(magn_signal / magn_other);

  // Amplitude is in source format; accum buf is 16-bit. For 8-bit sources, we
  // must compensate for the 8-bit shift before calculating output response.
  if (std::is_same<T, uint8_t>::value) {
    magn_signal /= 256.0;
  }
  return ValToDb(magn_signal / amplitude);
}

// Measure Frequency Response and SINAD for 1kHz sine from 8-bit source.
TEST(NoiseFloor, Source_8) {
  double magn_signal_db =
      MeasureSourceNoiseFloor<uint8_t>(&AudioResult::FloorSource8);

  EXPECT_GE(magn_signal_db, -AudioResult::kLevelToleranceSource8);
  EXPECT_LE(magn_signal_db, AudioResult::kLevelToleranceSource8);

  EXPECT_GE(AudioResult::FloorSource8, AudioResult::kPrevFloorSource8);
}

// Measure Frequency Response and SINAD for 1kHz sine from 16bit source.
TEST(NoiseFloor, Source_16) {
  double magn_signal_db =
      MeasureSourceNoiseFloor<int16_t>(&AudioResult::FloorSource16);

  EXPECT_GE(magn_signal_db, -AudioResult::kLevelToleranceSource16);
  EXPECT_LE(magn_signal_db, AudioResult::kLevelToleranceSource16);

  EXPECT_GE(AudioResult::FloorSource16, AudioResult::kPrevFloorSource16);
}

template <typename T>
double MeasureOutputNoiseFloor(double* magn_other_db) {
  audio::OutputFormatterPtr output_formatter;

  if (std::is_same<T, uint8_t>::value) {
    output_formatter = SelectOutputFormatter(AudioSampleFormat::UNSIGNED_8, 1);
  } else if (std::is_same<T, int16_t>::value) {
    output_formatter = SelectOutputFormatter(AudioSampleFormat::SIGNED_16, 1);
  } else {
    FXL_DCHECK(false) << "Unsupported source format";
  }

  const double amplitude = std::numeric_limits<int16_t>::max();

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
  *magn_other_db = ValToDb(magn_signal / magn_other);

  // Calculate magnitude of primary signal strength
  // Amplitude is ~int16:max regardless of output. For 8-bit outputs, we must
  // first compensate for the 8-bit shift before calculating output response.
  if (std::is_same<T, uint8_t>::value) {
    magn_signal *= 256.0;
  }

  return ValToDb(magn_signal / amplitude);
}

// Measure Frequency Response and SINAD for 1kHz sine, to an 8bit output.
TEST(NoiseFloor, Output_8) {
  double magn_signal_db =
      MeasureOutputNoiseFloor<uint8_t>(&AudioResult::FloorOutput8);

  EXPECT_GE(magn_signal_db, -AudioResult::kLevelToleranceOutput8);
  EXPECT_LE(magn_signal_db, AudioResult::kLevelToleranceOutput8);

  EXPECT_GE(AudioResult::FloorOutput8, AudioResult::kPrevFloorOutput8);
}

// Measure Frequency Response and SINAD for 1kHz sine, to a 16bit output.
TEST(NoiseFloor, Output_16) {
  double magn_signal_db =
      MeasureOutputNoiseFloor<int16_t>(&AudioResult::FloorOutput16);

  EXPECT_GE(magn_signal_db, -AudioResult::kLevelToleranceOutput16);
  EXPECT_LE(magn_signal_db, AudioResult::kLevelToleranceOutput16);

  EXPECT_GE(AudioResult::FloorOutput16, AudioResult::kPrevFloorOutput16);
}

}  // namespace test
}  // namespace media
