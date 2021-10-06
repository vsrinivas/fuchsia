// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_FIDELITY_TEST_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_FIDELITY_TEST_H_

#include <fuchsia/media/cpp/fidl.h>

#include <optional>
#include <set>
#include <string>

#include "src/media/audio/lib/test/hermetic_pipeline_test.h"

namespace media::audio::test {

// These tests feed a series of individual sinusoidal signals (across the
// frequency spectrum) into the pipeline, validating that the output level is
// (1) high at the expected frequency, and (2) low at all other frequencies
// (respectively, frequency response and signal-to-noise-and-distortion).
class HermeticFidelityTest : public HermeticPipelineTest {
 public:
  static constexpr int64_t kNumReferenceFreqs = 42;
  // The (approximate) frequencies represented by limit-threshold arrays freq_resp_lower_limits_db
  // and sinad_lower_limits_db, and corresponding actual results arrays gathered during the tests.
  // The actual frequencies (within a buffer of kFreqTestBufSize) are found in kRefFreqsInternal.
  static const std::array<int32_t, kNumReferenceFreqs> kReferenceFrequencies;

  // Test the three render paths present in common effects configurations.
  enum class RenderPath {
    Media = 0,
    Communications = 1,
    Ultrasound = 2,
  };
  // Specify an output channel to measure, and thresholds against which to compare it.
  struct ChannelMeasurement {
    int32_t channel;
    const std::array<double, kNumReferenceFreqs> freq_resp_lower_limits_db;
    const std::array<double, kNumReferenceFreqs> sinad_lower_limits_db;

    ChannelMeasurement(int32_t chan, std::array<double, kNumReferenceFreqs> freqs,
                       std::array<double, kNumReferenceFreqs> sinads)
        : channel(chan), freq_resp_lower_limits_db(freqs), sinad_lower_limits_db(sinads) {}
  };

  struct EffectConfig {
    std::string name;
    std::string config;
  };

  // This struct includes all the configuration info for this full-spectrum test.
  template <fuchsia::media::AudioSampleFormat InputFormat,
            fuchsia::media::AudioSampleFormat OutputFormat>
  struct TestCase {
    // This string must be unique among the different cases in this test suite.
    std::string test_name;

    TypedFormat<InputFormat> input_format;
    RenderPath path;
    const std::set<int32_t> channels_to_play;

    PipelineConstants pipeline;
    // TODO(fxbug.dev/85960): Add mechanism to specify that a single frequency should be tested.
    int32_t low_cut_frequency = 0;
    int32_t low_pass_frequency = fuchsia::media::MAX_PCM_FRAMES_PER_SECOND;
    std::optional<uint32_t> thermal_state = std::nullopt;

    TypedFormat<OutputFormat> output_format;
    const std::set<ChannelMeasurement> channels_to_measure;

    std::vector<EffectConfig> effect_configs;
  };

  void SetUp() override;

  template <fuchsia::media::AudioSampleFormat InputFormat,
            fuchsia::media::AudioSampleFormat OutputFormat>
  void Run(const TestCase<InputFormat, OutputFormat>& tc);

 protected:
  static inline double DoubleToDb(double val) { return std::log10(val) * 20.0; }

  static std::array<double, HermeticFidelityTest::kNumReferenceFreqs>& level_results(
      std::string test_name, int32_t channel);
  static std::array<double, HermeticFidelityTest::kNumReferenceFreqs>& sinad_results(
      std::string test_name, int32_t channel);

  void TranslateReferenceFrequencies(int32_t device_frame_rate);

  // Create a renderer for this path, submit and play the input buffer, retrieve the ring buffer.
  template <fuchsia::media::AudioSampleFormat InputFormat,
            fuchsia::media::AudioSampleFormat OutputFormat>
  AudioBuffer<OutputFormat> GetRendererOutput(TypedFormat<InputFormat> input_format,
                                              int64_t input_buffer_frames, RenderPath path,
                                              AudioBuffer<InputFormat> input,
                                              VirtualOutput<OutputFormat>* device);

  // Display results for this path, in tabular form for each compare/copy to existing limits.
  template <fuchsia::media::AudioSampleFormat InputFormat,
            fuchsia::media::AudioSampleFormat OutputFormat>
  void DisplaySummaryResults(const TestCase<InputFormat, OutputFormat>& test_case);

  // Validate results for the given channel set, against channel-mapped results arrays.
  template <fuchsia::media::AudioSampleFormat InputFormat,
            fuchsia::media::AudioSampleFormat OutputFormat>
  void VerifyResults(const TestCase<InputFormat, OutputFormat>& test_case);

  // Change the output pipeline's thermal state, blocking until the state change completes.
  zx_status_t ConfigurePipelineForThermal(uint32_t thermal_state);

 private:
  // Ref frequencies, internally translated to values corresponding to a buffer[kFreqTestBufSize].
  std::array<int32_t, kNumReferenceFreqs> translated_ref_freqs_;

  bool save_fidelity_wav_files_;
};

inline bool operator<(const HermeticFidelityTest::ChannelMeasurement& lhs,
                      const HermeticFidelityTest::ChannelMeasurement& rhs) {
  return (lhs.channel < rhs.channel);
}

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_FIDELITY_TEST_H_
