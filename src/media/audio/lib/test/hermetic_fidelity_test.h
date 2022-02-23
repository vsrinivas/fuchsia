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

  // Test renderers that use various reference clocks.  These options include:
  enum class ClockMode {
    // Client wants to use the reference clock we provide to clients that do not specify one. This
    // is not strongly specified, so clients using timestamps must call GetReferenceClock.
    Default,
    // Client set a ref clock that is intentionally invalid, to signal that they want the clock
    // created/maintained by AudioCore to smoothly track targeted audio device(s).
    Flexible,
    // Client sets a literal clone of CLOCK_MONOTONIC as the renderer's reference clock. "Clone"
    // means the clock has the same rate and offset as CLOCK_MONOTONIC.
    Monotonic,
    // Client sets a clock with monotonic rate and non-zero offset (NOT a CLOCK_MONOTONIC clone).
    // For these tests, the actual offset value does not matter, as long as it is non-zero.
    Offset,
    // Client sets a ref clock with rate other than CLOCK_MONOTONIC. Currently, no test uses this
    // option: it exists only to differentiate from Offset (which runs at monotonic rate).
    RateAdjusted,
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
    ClockMode renderer_clock_mode = ClockMode::Default;

    PipelineConstants pipeline;
    // Only test this one frequency, not the full-spectrum set.
    std::optional<int32_t> single_frequency_to_test;
    // Regardless of the input frequencies used, expect no output frequencies below this value.
    int32_t low_cut_frequency = 0;
    // Regardless of the input frequencies used, expect no output frequencies above this value.
    std::optional<int32_t> low_pass_frequency;
    // If specified, the thermal state to put the pipeline into, before assessing its fidelity.
    std::optional<uint32_t> thermal_state;

    std::optional<audio_stream_unique_id_t> device_id;
    TypedFormat<OutputFormat> output_format;
    const std::set<ChannelMeasurement> channels_to_measure;

    std::vector<EffectConfig> effect_configs;
  };

  static const std::array<double, HermeticFidelityTest::kNumReferenceFreqs> FillArray(double val);

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

  struct Frequency {
    int32_t display_val;
    int32_t periods;
    size_t idx;
  };

  struct SignalSectionIndices {
    int64_t stabilization_start;
    int64_t analysis_start;
    int64_t analysis_end;
    int64_t stabilization_end;
  };

  void TearDown() override {
    if constexpr (!kEnableAllOverflowAndUnderflowChecksInRealtimeTests) {
      // Even if the system cannot guarantee real-time response, we expect no renderer underflows
      // because we submit the whole signal before calling Play(). Keep that check enabled.
      ExpectNoRendererUnderflows();
    }
    HermeticPipelineTest::TearDown();
  }

  int32_t FrequencyToPeriods(int32_t device_frame_rate, int32_t frequency);
  template <fuchsia::media::AudioSampleFormat InputFormat,
            fuchsia::media::AudioSampleFormat OutputFormat>
  std::vector<Frequency> GetTestFrequencies(
      const HermeticFidelityTest::TestCase<InputFormat, OutputFormat>& tc);

  // Create a renderer for this path, submit and play the input buffer, retrieve the ring buffer.
  template <fuchsia::media::AudioSampleFormat InputFormat,
            fuchsia::media::AudioSampleFormat OutputFormat>
  AudioBuffer<OutputFormat> GetRendererOutput(TypedFormat<InputFormat> input_format,
                                              int64_t input_buffer_frames, RenderPath path,
                                              AudioBuffer<InputFormat> input,
                                              VirtualOutput<OutputFormat>* device,
                                              ClockMode clock_mode);

  // Display specific locations of interest in the generated input signal, for debugging.
  template <fuchsia::media::AudioSampleFormat InputFormat>
  static void DisplayInputBufferSections(const AudioBuffer<InputFormat>& buffer,
                                         const std::string& initial_tag,
                                         const SignalSectionIndices& input_indices);

  // Display specific locations of interest in the received output signal, for debugging.
  template <fuchsia::media::AudioSampleFormat OutputFormat>
  static void DisplayOutputBufferSections(const AudioBuffer<OutputFormat>& buffer,
                                          const std::string& initial_tag,
                                          const SignalSectionIndices& output_indices);

  // Display results for this path, in tabular form for each compare/copy to existing limits.
  template <fuchsia::media::AudioSampleFormat InputFormat,
            fuchsia::media::AudioSampleFormat OutputFormat>
  void DisplaySummaryResults(
      const TestCase<InputFormat, OutputFormat>& test_case,
      const std::vector<HermeticFidelityTest::Frequency>& frequencies_to_display);

  // Validate results for the given channel set, against channel-mapped results arrays.
  template <fuchsia::media::AudioSampleFormat InputFormat,
            fuchsia::media::AudioSampleFormat OutputFormat>
  void VerifyResults(const TestCase<InputFormat, OutputFormat>& test_case,
                     const std::vector<HermeticFidelityTest::Frequency>& frequencies_to_verify);

  // Change the output pipeline's thermal state, blocking until the state change completes.
  zx_status_t ConfigurePipelineForThermal(uint32_t thermal_state);

 private:
  bool save_fidelity_wav_files_;
};

inline bool operator<(const HermeticFidelityTest::ChannelMeasurement& lhs,
                      const HermeticFidelityTest::ChannelMeasurement& rhs) {
  return (lhs.channel < rhs.channel);
}

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_FIDELITY_TEST_H_
