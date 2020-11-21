// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_PIPELINE_TEST_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_PIPELINE_TEST_H_

#include <string>
#include <vector>

#include "src/media/audio/lib/test/hermetic_audio_test.h"

namespace media::audio::test {

// This class defines a framework for standard tests of an output pipeline. After feeding an
// arbitrary input signal through the pipeline and capturing the output, this framework can ensure
// that the output (for example) approximately matches an expected "golden" signal, or contains
// timing-oriented impulses at expected locations, or meets an expected frequency profile.
class HermeticPipelineTest : public HermeticAudioTest {
 public:
  void TearDown() override {
    // None of these tests should have overflows or underflows.
    ExpectNoOverflowsOrUnderflows();
    HermeticAudioTest::TearDown();
  }

  struct PipelineConstants {
    // The pipeline's positive and negative filter widths, in units of source frames.
    // These correspond to the sum of widths for all output pipeline components.
    //
    // These two durations lead to the "cross-fade" observed in an output, at transitions between
    // input signals or between silence and signal. Some call these output intervals (respectively)
    // "pre-ramp"/"ring in" (before transition) and "post-ramp"/"ring out" (after transition).
    //
    // For a signal Input that extends from frame X to frame Y, it is only for source positions
    // [X+neg_filter_width, Y-pos_filter_width] that corresponding Output is based PURELY on Input
    // content. Outside this, Output is also affected by what is immediately before/after Input.
    //
    // Restated, producing Output that corresponds to source frame range [X, Y] will actually depend
    // on the content of Input frames [X-neg_filter_width, Y+pos_filter_width].
    //
    // These should be upper-bounds; they don't need to be exact.
    size_t pos_filter_width = 0;
    size_t neg_filter_width = 0;

    // Gain of the pipeline's output device.
    // The test will assert that the output device is created with device gain set to this value.
    float output_device_gain_db = 0;
  };

  // Each test can compute a precise number of expected output frames given the number of
  // input frames. Our device ring buffer includes more frames than necessary so that, in
  // case we write too many output frames due to a bug, we'll have plenty of space without
  // wrapping around. This helps more easily detect such bugs.
  static inline size_t AddSlackToOutputFrames(size_t expected_output_frames) {
    return static_cast<size_t>(static_cast<double>(expected_output_frames) * 1.5);
  }

  template <fuchsia::media::AudioSampleFormat SampleFormat>
  static void WriteWavFile(const std::string& test_name, const std::string& file_name_suffix,
                           AudioBufferSlice<SampleFormat> slice);

  static bool save_input_and_output_files_;
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_PIPELINE_TEST_H_
