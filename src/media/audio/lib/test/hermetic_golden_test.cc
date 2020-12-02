// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/test/hermetic_golden_test.h"

#include <fuchsia/media/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/media/audio/lib/analysis/analysis.h"
#include "src/media/audio/lib/analysis/generators.h"
#include "src/media/audio/lib/format/audio_buffer.h"
#include "src/media/audio/lib/test/comparators.h"
#include "src/media/audio/lib/test/hermetic_pipeline_test.h"
#include "src/media/audio/lib/test/renderer_shim.h"
#include "src/media/audio/lib/wav/wav_writer.h"

using ASF = fuchsia::media::AudioSampleFormat;

namespace media::audio::test {

namespace {

// Using a macro here to preserve line numbers in the test error messages.
#define EXPECT_WITHIN_RELATIVE_ERROR(actual, expected, threshold)                          \
  do {                                                                                     \
    auto label =                                                                           \
        fxl::StringPrintf("\n  %s = %f\n  %s = %f", #actual, actual, #expected, expected); \
    EXPECT_NE(expected, 0) << label;                                                       \
    if (expected != 0) {                                                                   \
      auto err = abs(actual - expected) / expected;                                        \
      EXPECT_LE(err, threshold) << label;                                                  \
    }                                                                                      \
  } while (0)

// WaveformTestRunner wraps some methods used by RunTestCase.
template <ASF InputFormat, ASF OutputFormat>
class WaveformTestRunner {
 public:
  WaveformTestRunner(const HermeticGoldenTest::TestCase<InputFormat, OutputFormat>& tc) : tc_(tc) {}

  void CompareRMSE(AudioBufferSlice<OutputFormat> actual, AudioBufferSlice<OutputFormat> expected);
  void CompareRMS(AudioBufferSlice<OutputFormat> actual, AudioBufferSlice<OutputFormat> expected);
  void CompareFreqs(AudioBufferSlice<OutputFormat> actual, AudioBufferSlice<OutputFormat> expected,
                    std::vector<size_t> hz_signals);

  void ExpectSilence(AudioBufferSlice<OutputFormat> actual);

 private:
  const HermeticGoldenTest::TestCase<InputFormat, OutputFormat>& tc_;
};

template <ASF InputFormat, ASF OutputFormat>
void WaveformTestRunner<InputFormat, OutputFormat>::CompareRMSE(
    AudioBufferSlice<OutputFormat> actual, AudioBufferSlice<OutputFormat> expected) {
  CompareAudioBuffers(actual, expected,
                      {
                          .max_relative_error = tc_.max_relative_rms_error,
                          .test_label = "check data",
                          .num_frames_per_packet = expected.format().frames_per_second() / 1000 *
                                                   RendererShimImpl::kPacketMs,
                      });
}

template <ASF InputFormat, ASF OutputFormat>
void WaveformTestRunner<InputFormat, OutputFormat>::CompareRMS(
    AudioBufferSlice<OutputFormat> actual, AudioBufferSlice<OutputFormat> expected) {
  auto expected_rms = MeasureAudioRMS(expected);
  auto actual_rms = MeasureAudioRMS(actual);
  EXPECT_WITHIN_RELATIVE_ERROR(actual_rms, expected_rms, tc_.max_relative_rms);
}

template <ASF InputFormat, ASF OutputFormat>
void WaveformTestRunner<InputFormat, OutputFormat>::CompareFreqs(
    AudioBufferSlice<OutputFormat> actual, AudioBufferSlice<OutputFormat> expected,
    std::vector<size_t> hz_signals) {
  ASSERT_EQ(expected.NumFrames(), actual.NumFrames());

  // FFT requires a power-of-2 number of samples.
  auto expected_buf = PadToNearestPower2(expected);
  auto actual_buf = PadToNearestPower2(actual);
  expected = AudioBufferSlice(&expected_buf);
  actual = AudioBufferSlice(&actual_buf);

  // Translate hz to periods.
  std::unordered_set<size_t> freqs_in_unit_periods;
  for (auto hz : hz_signals) {
    ASSERT_GT(hz, 0u);

    // Frames per period at frequency hz.
    size_t fpp = expected.format().frames_per_second() / hz;

    // If there are an integer number of periods, we can precisely measure the magnitude at hz.
    // Otherwise, the magnitude will be smeared between the two adjacent integers.
    size_t periods = expected.NumFrames() / fpp;
    freqs_in_unit_periods.insert(periods);
    if (expected.NumFrames() % fpp > 0) {
      freqs_in_unit_periods.insert(periods + 1);
    }
  }

  auto expected_result = MeasureAudioFreqs(expected, freqs_in_unit_periods);
  auto actual_result = MeasureAudioFreqs(actual, freqs_in_unit_periods);

  EXPECT_WITHIN_RELATIVE_ERROR(actual_result.total_magn_signal, expected_result.total_magn_signal,
                               tc_.max_relative_signal_error);
  EXPECT_WITHIN_RELATIVE_ERROR(actual_result.total_magn_other, expected_result.total_magn_other,
                               tc_.max_relative_other_error);

  for (auto periods : freqs_in_unit_periods) {
    auto hz = static_cast<double>(periods) *
              static_cast<double>(expected.format().frames_per_second()) /
              static_cast<double>(expected.NumFrames());
    SCOPED_TRACE(testing::Message() << "Frequency " << periods << " periods, " << hz << " hz");

    EXPECT_WITHIN_RELATIVE_ERROR(actual_result.magnitudes[periods],
                                 expected_result.magnitudes[periods],
                                 tc_.max_relative_signal_error);

    EXPECT_WITHIN_RELATIVE_ERROR(actual_result.phases[periods], expected_result.phases[periods],
                                 tc_.max_relative_signal_phase_error);
  }
}

template <ASF InputFormat, ASF OutputFormat>
void WaveformTestRunner<InputFormat, OutputFormat>::ExpectSilence(
    AudioBufferSlice<OutputFormat> actual) {
  CompareAudioBuffers(actual, AudioBufferSlice<OutputFormat>(),
                      {
                          .test_label = "check silence",
                          .num_frames_per_packet = actual.format().frames_per_second() / 1000 *
                                                   RendererShimImpl::kPacketMs,
                      });
}

}  // namespace

template <ASF InputFormat, ASF OutputFormat>
void HermeticGoldenTest::Run(const HermeticGoldenTest::TestCase<InputFormat, OutputFormat>& tc) {
  WaveformTestRunner<InputFormat, OutputFormat> runner(tc);

  const auto& input = tc.input;
  const auto& expected_output = tc.expected_output;

  auto device = CreateOutput(AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS, expected_output.format(),
                             AddSlackToOutputFrames(expected_output.NumFrames()), std::nullopt,
                             tc.pipeline.output_device_gain_db);
  auto renderer = CreateAudioRenderer(input.format(), input.NumFrames());

  // Render the input at a time such that the first frame of audio will be rendered into
  // the first frame of the ring buffer. We need to synchronize with the ring buffer, then
  // leave some silence to account for ring in.
  auto packets = renderer->AppendPackets({&input});
  auto min_start_time = zx::clock::get_monotonic() + renderer->min_lead_time() + zx::msec(20);
  auto start_time =
      device->NextSynchronizedTimestamp(min_start_time) +
      zx::nsec(renderer->format().frames_per_ns().Inverse().Scale(tc.pipeline.neg_filter_width));
  renderer->Play(this, start_time, 0);
  renderer->WaitForPackets(this, packets);

  // The ring buffer should contain the expected output followed by silence.
  auto ring_buffer = device->SnapshotRingBuffer();
  auto num_data_frames = expected_output.NumFrames();
  auto output_data = AudioBufferSlice(&ring_buffer, 0, num_data_frames);
  auto output_silence = AudioBufferSlice(&ring_buffer, num_data_frames, device->frame_count());

  if (save_input_and_output_files_) {
    WriteWavFile<InputFormat>(tc.test_name, "input", &input);
    WriteWavFile<OutputFormat>(tc.test_name, "ring_buffer", &ring_buffer);
    WriteWavFile<OutputFormat>(tc.test_name, "output", output_data);
    WriteWavFile<OutputFormat>(tc.test_name, "expected_output", &expected_output);
  }

  runner.CompareRMSE(&expected_output, output_data);
  runner.ExpectSilence(output_silence);

  for (size_t chan = 0; chan < expected_output.format().channels(); chan++) {
    auto expected_chan = AudioBufferSlice<OutputFormat>(&expected_output).GetChannel(chan);
    auto output_chan = output_data.GetChannel(chan);
    SCOPED_TRACE(testing::Message() << "Channel " << chan);
    runner.CompareRMS(&output_chan, &expected_chan);
    runner.CompareFreqs(&output_chan, &expected_chan, tc.frequencies_hz_to_analyze);
  }
}

// Explicitly instantiate (almost) all possible implementations.
// We intentionally don't instantiate implementations with OutputFormat = UNSIGNED_8
// because such hardware is no longer in use, therefore it's not worth testing.
#define INSTANTIATE(InputFormat, OutputFormat)                      \
  template void HermeticGoldenTest::Run<InputFormat, OutputFormat>( \
      const TestCase<InputFormat, OutputFormat>& tc);

INSTANTIATE(ASF::UNSIGNED_8, ASF::SIGNED_16)
INSTANTIATE(ASF::UNSIGNED_8, ASF::SIGNED_24_IN_32)
INSTANTIATE(ASF::UNSIGNED_8, ASF::FLOAT)

INSTANTIATE(ASF::SIGNED_16, ASF::SIGNED_16)
INSTANTIATE(ASF::SIGNED_16, ASF::SIGNED_24_IN_32)
INSTANTIATE(ASF::SIGNED_16, ASF::FLOAT)

INSTANTIATE(ASF::SIGNED_24_IN_32, ASF::SIGNED_16)
INSTANTIATE(ASF::SIGNED_24_IN_32, ASF::SIGNED_24_IN_32)
INSTANTIATE(ASF::SIGNED_24_IN_32, ASF::FLOAT)

INSTANTIATE(ASF::FLOAT, ASF::SIGNED_16)
INSTANTIATE(ASF::FLOAT, ASF::SIGNED_24_IN_32)
INSTANTIATE(ASF::FLOAT, ASF::FLOAT)

}  // namespace media::audio::test
