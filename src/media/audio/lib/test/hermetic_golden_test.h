// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_GOLDEN_TEST_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_GOLDEN_TEST_H_

#include <string>
#include <vector>

#include "src/media/audio/lib/test/hermetic_audio_test.h"

namespace media::audio::test {

// This class defines a framework for standard tests of an output pipeline.
// There are two kinds of tests:
//
// 1. Waveform tests feed in an arbitrary waveform and validate that the output
//    is an approximate match for an expected "golden" output.
//
// 2. Impulse tests feed in a sequence of impulses and validate that they appear
//    in the output at the correct locations.
//
class HermeticGoldenTest : public HermeticAudioTest {
 public:
  void TearDown() {
    // None of these tests should have overflows or underflows.
    ExpectNoOverflowsOrUnderflows();
    HermeticAudioTest::TearDown();
  }

  struct PipelineConstants {
    // The positive and negative filter widths of the output pipeline, in unit frames.
    // The positive width describes how far forward the filter looks.
    // The negative width describes how far backward the filter looks.
    //
    // If we play a sound at frame X that lasts until frame Y, we expect sound within the
    // range [X-pos_filter_width, Y+neg_filter_width] and silence outside of that range.
    //
    // Put differently, pos_filter_width gives the number of "ring in" or "fade in" frames
    // while neg_filter_width gives the number of "ring out" or "fade out" frames.
    //
    // These should be upper-bounds; they don't need to be exact.
    size_t pos_filter_width = 0;
    size_t neg_filter_width = 0;
  };

  ////////////////////////////////////////////////////////////////////////////////////////
  // Waveform tests
  //
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

  template <fuchsia::media::AudioSampleFormat InputFormat,
            fuchsia::media::AudioSampleFormat OutputFormat>
  struct WaveformTestCase {
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
  void RunWaveformTest(const WaveformTestCase<InputFormat, OutputFormat>& tc);

  ////////////////////////////////////////////////////////////////////////////////////////
  // Impulse tests
  //
  // These tests feed one or more impulses into a pipeline, producing an output buffer,
  // then validate that the impulses appear at the correct positions in the output.

  template <fuchsia::media::AudioSampleFormat InputFormat,
            fuchsia::media::AudioSampleFormat OutputFormat>
  struct ImpulseTestCase {
    std::string test_name;
    PipelineConstants pipeline;

    TypedFormat<InputFormat> input_format;
    TypedFormat<OutputFormat> output_format;

    // Width, height, and location of the input impulses. Impulses should be separated by
    // at least pipeline.pos_filter_width + pipeline.neg_filter_width frames.
    size_t impulse_width_in_frames;
    typename SampleFormatTraits<InputFormat>::SampleT impulse_magnitude;
    std::vector<size_t> impulse_locations_in_frames;
  };

  template <fuchsia::media::AudioSampleFormat InputFormat,
            fuchsia::media::AudioSampleFormat OutputFormat>
  void RunImpulseTest(const ImpulseTestCase<InputFormat, OutputFormat>& tc);
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_GOLDEN_TEST_H_
