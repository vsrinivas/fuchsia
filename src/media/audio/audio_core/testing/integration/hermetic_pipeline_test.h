// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_INTEGRATION_HERMETIC_PIPELINE_TEST_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_INTEGRATION_HERMETIC_PIPELINE_TEST_H_

#include <string>
#include <vector>

#include "src/media/audio/audio_core/testing/integration/hermetic_audio_test.h"

namespace media::audio::test {

// This class defines a framework for standard tests of an output pipeline. After feeding an
// arbitrary input signal through the pipeline and capturing the output, this framework can ensure
// that the output meets specific criteria -- for example, meets an expected frequency profile.
class HermeticPipelineTest : public HermeticAudioTest {
 public:
  void TearDown() override {
    if constexpr (kEnableAllOverflowAndUnderflowChecksInRealtimeTests) {
      // None of these tests should have overflows or underflows.
      ExpectNoOverflowsOrUnderflows();
    }
    HermeticAudioTest::TearDown();
  }

  // The three render paths present in common effects configurations.
  enum class RenderPath {
    Media = 0,
    Communications = 1,
    Ultrasound = 2,
  };

  struct PipelineConstants {
    // The pipeline's transition widths, in units of source frames.
    // These correspond to the sum of widths for all output (or input) pipeline components.
    //
    // The first two durations encompass the "fade-in" observed in an output, when the input signal
    // transitions from silence to signal. This transition is divided into the pre-transition
    // "ramp-in" and the post-transition "stabilization".
    //
    // The next two durations encompass the "fade-out" observed in an output, when the input signal
    // transitions from signal to silence. This transition is divided into the pre-transition
    // "destabilization" and the post-transition "decay".
    //
    // For an Input signal extending from frame X to frame Y, we expect the following in the Output:
    // - silence for positions corresponding to source positions before X-ramp_in_width;
    // - transitional values corresponding to source range [X-ramp_in_width, X+stabilization_width];
    // - pure "signal" values ONLY for output positions corresponding to source position range
    //   [X+stabilization_width, Y-destabilization_width];
    // - transitional values corresponding to source range [Y-destabilization_width, Y+decay_width];
    // - silence for positions corresponding to source positions after Y+decay_width.
    //
    // Restated, producing Output that corresponds to source frame range [X, Y] will actually depend
    // on the content of Input frames [X-decay_width, Y+ramp_in_width].
    //
    // These should be upper-bounds; they don't need to be exact.
    size_t ramp_in_width = 0;
    size_t stabilization_width = 0;
    size_t destabilization_width = 0;
    size_t decay_width = 0;

    // These fields are present for legacy reasons; they will be removed.
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

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_INTEGRATION_HERMETIC_PIPELINE_TEST_H_
