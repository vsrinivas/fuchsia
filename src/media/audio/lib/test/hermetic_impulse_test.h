// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_IMPULSE_TEST_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_IMPULSE_TEST_H_

#include <string>
#include <vector>

#include "src/media/audio/lib/test/hermetic_pipeline_test.h"

namespace media::audio::test {

// These tests feed one or more impulses into a pipeline, producing an output buffer,
// then validate that the impulses appear at the correct positions in the output.
class HermeticImpulseTest : public HermeticPipelineTest {
 public:
  template <fuchsia::media::AudioSampleFormat InputFormat,
            fuchsia::media::AudioSampleFormat OutputFormat>
  struct TestCase {
    std::string test_name;
    PipelineConstants pipeline;

    TypedFormat<InputFormat> input_format;
    TypedFormat<OutputFormat> output_format;

    // Width, height, and location of the input impulses. Impulses should be separated by
    // at least pipeline.pre_end_ramp_frames + pipeline.post_start_ramp_frames.
    size_t impulse_width_in_frames;
    typename SampleFormatTraits<InputFormat>::SampleT impulse_magnitude;
    std::vector<size_t> impulse_locations_in_frames;
  };

  template <fuchsia::media::AudioSampleFormat InputFormat,
            fuchsia::media::AudioSampleFormat OutputFormat>
  void Run(const TestCase<InputFormat, OutputFormat>& tc);
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_IMPULSE_TEST_H_
