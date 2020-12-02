// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_GOLDEN_TEST_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_GOLDEN_TEST_H_

#include <string>
#include <vector>

#include "src/media/audio/lib/test/hermetic_pipeline_test.h"

namespace media::audio::test {

// These tests feed an input waveform into a pipeline, producing an output waveform,
// which is then compared against an expected output waveform in the following ways:
//
// 1. Ensure RMSE < threshold, where "RMSE" is the "RMS Error", computed as the RMS of
//    the difference between the actual and expected outputs. This validates that the
//    output approximately matches the input.
//
// 2. Ensure RMS ~= expected RMS. This validates loudness of the output audio. This is
//    technically subsumed by RMSE, but included to help identify cases where the output
//    differs from the expected output by just volume, not shape.
//
// 3. Ensure FFT(x) ~= expected magnitude. This uses an FFT to compute the magnitude of
//    the output signal at a given set of frequencies, then compares those magnitudes to
//    an FFT computed on the expected output. This validates that the output has the
//    expected frequency response.
//
// Together, these three comparisons ensure that the actual output audio is approximately
// equal to the expected output, within thresholds defined by the test case.
class HermeticGoldenTest : public HermeticPipelineTest {
 public:
  template <fuchsia::media::AudioSampleFormat InputFormat,
            fuchsia::media::AudioSampleFormat OutputFormat>
  struct TestCase {
    std::string test_name;
    PipelineConstants pipeline;

    AudioBuffer<InputFormat> input;
    AudioBuffer<OutputFormat> expected_output;

    // For RMSE (RMS error) comparisons.
    // This value is passed to CompareAudioBufferOptions.max_relative_error.
    // https://en.wikipedia.org/wiki/Root-mean-square_deviation
    float max_relative_rms_error;

    // For RMS comparisons.
    // The output's RMS must be within max_relative_rms * RMS(golden).
    // https://en.wikipedia.org/wiki/Root_mean_square
    float max_relative_rms;

    // For FFT comparisons, these are relative error thresholds. "signal" represents the
    // frequency of measurement and "other" represents the total of all other frequencies.
    // See MeasureAudioFreq.
    float max_relative_signal_phase_error;
    float max_relative_signal_error;
    float max_relative_other_error;

    // A set of frequencies to compare in the input vs output using an FFT analysis.
    // Frequencies are specified in hz.
    std::vector<size_t> frequencies_hz_to_analyze;
  };

  template <fuchsia::media::AudioSampleFormat InputFormat,
            fuchsia::media::AudioSampleFormat OutputFormat>
  void Run(const TestCase<InputFormat, OutputFormat>& tc);

  // TODO(mpuryear): remove the below, once clients have moved to simpler names
  template <fuchsia::media::AudioSampleFormat InputFormat,
            fuchsia::media::AudioSampleFormat OutputFormat>
  using WaveformTestCase = TestCase<InputFormat, OutputFormat>;

  template <fuchsia::media::AudioSampleFormat InputFormat,
            fuchsia::media::AudioSampleFormat OutputFormat>
  void RunWaveformTest(const TestCase<InputFormat, OutputFormat>& tc) {
    Run(tc);
  }
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_GOLDEN_TEST_H_
