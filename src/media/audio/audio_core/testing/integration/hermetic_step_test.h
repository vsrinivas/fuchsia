// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_INTEGRATION_HERMETIC_STEP_TEST_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_INTEGRATION_HERMETIC_STEP_TEST_H_

#include <optional>
#include <string>
#include <vector>

#include "src/media/audio/audio_core/testing/integration/hermetic_pipeline_test.h"
#include "src/media/audio/lib/format/format.h"

namespace media::audio::test {

// These tests feed a constant-value step into a pipeline, producing an output buffer,
// then validate that the output buffer's step magnitude is the expected value.
class HermeticStepTest : public HermeticPipelineTest {
 public:
  template <fuchsia::media::AudioSampleFormat InputFormat,
            fuchsia::media::AudioSampleFormat OutputFormat>
  struct TestCase {
    std::string test_name;
    TypedFormat<InputFormat> input_format;
    // Width and height of the input step.
    typename SampleFormatTraits<InputFormat>::SampleT source_step_magnitude;
    int64_t source_step_width_in_frames;

    RenderPath path;
    // Ramp and stabilization widths, to support non-unity SRC or effects with width.
    PipelineConstants pipeline;
    // If specified, applies renderer gain. To be used in dynamic-range and gain-limit testing.
    std::optional<float> gain_db;
    // If specified, put the pipeline into this thermal state before measuring the step.
    std::optional<uint32_t> thermal_state;

    TypedFormat<OutputFormat> output_format;
    typename SampleFormatTraits<OutputFormat>::SampleT expected_output_magnitude;
    typename SampleFormatTraits<OutputFormat>::SampleT output_magnitude_tolerance =
        std::numeric_limits<typename SampleFormatTraits<OutputFormat>::SampleT>::epsilon();
  };

  template <fuchsia::media::AudioSampleFormat InputFormat,
            fuchsia::media::AudioSampleFormat OutputFormat>
  void Run(const TestCase<InputFormat, OutputFormat>& tc);

 protected:
  void TearDown() override {
    if constexpr (!kEnableAllOverflowAndUnderflowChecksInRealtimeTests) {
      // Even if the system cannot guarantee real-time response, we expect no renderer underflows
      // because we submit the whole signal before calling Play(). Keep that check enabled.
      ExpectNoRendererUnderflows();
    }
    HermeticPipelineTest::TearDown();
  }
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_INTEGRATION_HERMETIC_STEP_TEST_H_
