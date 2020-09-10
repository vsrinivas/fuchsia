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
#include "src/media/audio/lib/test/renderer_shim.h"
#include "src/media/audio/lib/wav/wav_writer.h"

using ASF = fuchsia::media::AudioSampleFormat;

namespace media::audio::test {

//
// Command line flags set in hermetic_golden_test_main.cc.
//

// When enabled, save input and output as WAV files for comparison to the golden outputs.
// The saved files are:
//
//    <testname>_input.wav           - the input audio buffer
//    <testname>_ring_buffer.wav     - contents of the entire output ring buffer
//    <testname>_output.wav          - portion of the output ring buffer expected to be non-silent
//    <testname>_expected_output.wav - expected contents of <testname>_output.wav
//
// See
// ./hermetic_golden_test_update_goldens.sh for a semi-automated process.
bool flag_save_inputs_and_outputs = false;

namespace {

// Each test can compute a precise number of expected output frames given the number of
// input frames. Our device ring buffer includes frames than necessary so that, in case
// we write too many output frames due to a bug, we'll have plenty of space without
// wrapping around. This helps more easily detect such bugs.
size_t AddSlackToOutputFrames(size_t expected_output_frames) {
  return static_cast<size_t>(static_cast<double>(expected_output_frames) * 1.5);
}

// Using a macro here to preserve line numbers in the test error messages.
#define EXPECT_WITHIN_RELATIVE_ERROR(actual, expected, threshold)                          \
  do {                                                                                     \
    auto label =                                                                           \
        fxl::StringPrintf("\n  %s = %f\n  %s = %f", #actual, actual, #expected, expected); \
    FX_CHECK(expected != 0) << label;                                                      \
    auto err = abs(actual - expected) / expected;                                          \
    EXPECT_LE(err, threshold) << label;                                                    \
  } while (0)

template <ASF SampleFormat>
void WriteWavFile(const std::string& test_name, const std::string& file_name_suffix,
                  AudioBufferSlice<SampleFormat> slice) {
  WavWriter<true> w;
  auto file_name = "/cache/" + test_name + "_" + file_name_suffix + ".wav";
  auto& format = slice.format();
  if (!w.Initialize(file_name.c_str(), format.sample_format(), format.channels(),
                    format.frames_per_second(), format.bytes_per_frame() * 8 / format.channels())) {
    FX_LOGS(ERROR) << "Could not create output file " << file_name;
    return;
  }
  // TODO(fxbug.dev/52161): WavWriter.Write() should take const data
  auto ok =
      w.Write(
          const_cast<typename AudioBufferSlice<SampleFormat>::SampleT*>(&slice.buf()->samples()[0]),
          slice.NumBytes()) &&
      w.UpdateHeader() && w.Close();
  if (!ok) {
    FX_LOGS(ERROR) << "Error writing to output file " << file_name;
  } else {
    FX_LOGS(INFO) << "Wrote output file " << file_name;
  }
}

// WaveformTestRunner wraps some methods used by RunTestCase.
template <ASF InputF, ASF OutputF>
class WaveformTestRunner {
 public:
  WaveformTestRunner(const HermeticGoldenTest::WaveformTestCase<InputF, OutputF>& tc) : tc_(tc) {}

  void CompareRMSE(AudioBufferSlice<OutputF> actual, AudioBufferSlice<OutputF> expected);
  void CompareRMS(AudioBufferSlice<OutputF> actual, AudioBufferSlice<OutputF> expected);
  void CompareFreqs(AudioBufferSlice<OutputF> actual, AudioBufferSlice<OutputF> expected,
                    std::vector<size_t> hz_signals);

  void ExpectSilence(AudioBufferSlice<OutputF> actual);

 private:
  const HermeticGoldenTest::WaveformTestCase<InputF, OutputF>& tc_;
};

template <ASF InputF, ASF OutputF>
void WaveformTestRunner<InputF, OutputF>::CompareRMSE(AudioBufferSlice<OutputF> actual,
                                                      AudioBufferSlice<OutputF> expected) {
  CompareAudioBuffers(actual, expected,
                      {
                          .max_relative_error = tc_.max_relative_rms_error,
                          .test_label = "check data",
                          .num_frames_per_packet = expected.format().frames_per_second() / 1000 *
                                                   RendererShimImpl::kPacketMs,
                      });
}

template <ASF InputF, ASF OutputF>
void WaveformTestRunner<InputF, OutputF>::CompareRMS(AudioBufferSlice<OutputF> actual,
                                                     AudioBufferSlice<OutputF> expected) {
  auto expected_rms = MeasureAudioRMS(expected);
  auto actual_rms = MeasureAudioRMS(actual);
  EXPECT_WITHIN_RELATIVE_ERROR(actual_rms, expected_rms, tc_.max_relative_rms);
}

template <ASF InputF, ASF OutputF>
void WaveformTestRunner<InputF, OutputF>::CompareFreqs(AudioBufferSlice<OutputF> actual,
                                                       AudioBufferSlice<OutputF> expected,
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

template <ASF InputF, ASF OutputF>
void WaveformTestRunner<InputF, OutputF>::ExpectSilence(AudioBufferSlice<OutputF> actual) {
  CompareAudioBuffers(actual, AudioBufferSlice<OutputF>(),
                      {
                          .test_label = "check silence",
                          .num_frames_per_packet = actual.format().frames_per_second() / 1000 *
                                                   RendererShimImpl::kPacketMs,
                      });
}

}  // namespace

template <ASF InputF, ASF OutputF>
void HermeticGoldenTest::RunWaveformTest(
    const HermeticGoldenTest::WaveformTestCase<InputF, OutputF>& tc) {
  WaveformTestRunner<InputF, OutputF> runner(tc);

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
  auto min_start_time = zx::clock::get_monotonic() + renderer->GetMinLeadTime() + zx::msec(20);
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

  if (flag_save_inputs_and_outputs) {
    WriteWavFile<InputF>(tc.test_name, "input", &input);
    WriteWavFile<OutputF>(tc.test_name, "ring_buffer", &ring_buffer);
    WriteWavFile<OutputF>(tc.test_name, "output", output_data);
    WriteWavFile<OutputF>(tc.test_name, "expected_output", &expected_output);
  }

  runner.CompareRMSE(&expected_output, output_data);
  runner.ExpectSilence(output_silence);

  for (size_t chan = 0; chan < expected_output.format().channels(); chan++) {
    auto expected_chan = AudioBufferSlice<OutputF>(&expected_output).GetChannel(chan);
    auto output_chan = output_data.GetChannel(chan);
    SCOPED_TRACE(testing::Message() << "Channel " << chan);
    runner.CompareRMS(&output_chan, &expected_chan);
    runner.CompareFreqs(&output_chan, &expected_chan, tc.frequencies_hz_to_analyze);
  }
}

template <ASF InputF, ASF OutputF>
void HermeticGoldenTest::RunImpulseTest(
    const HermeticGoldenTest::ImpulseTestCase<InputF, OutputF>& tc) {
  // Compute the number of input frames.
  auto start_of_last_impulse = tc.impulse_locations_in_frames.back();
  auto num_input_frames = start_of_last_impulse + tc.impulse_width_in_frames +
                          tc.pipeline.pos_filter_width + tc.pipeline.neg_filter_width;

  // Helper to translate from an input frame number to an output frame number.
  auto input_frame_to_output_frame = [&tc](size_t input_frame) {
    auto input_fps = static_cast<double>(tc.input_format.frames_per_second());
    auto output_fps = static_cast<double>(tc.output_format.frames_per_second());
    return static_cast<size_t>(std::ceil(output_fps / input_fps * input_frame));
  };

  auto num_output_frames = input_frame_to_output_frame(num_input_frames);
  auto device = CreateOutput(AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS, tc.output_format,
                             AddSlackToOutputFrames(num_output_frames), std::nullopt,
                             tc.pipeline.output_device_gain_db);
  auto renderer = CreateAudioRenderer(tc.input_format, num_input_frames);

  // Write all of the impulses to an input buffer so we can easily write the full
  // input to a WAV file for debugging. Include silence at the beginning to account
  // for ring in; this allows us to align the input and output WAV files.
  auto impulse_start = tc.pipeline.neg_filter_width;
  AudioBuffer<InputF> input(tc.input_format, num_input_frames);
  for (auto start_frame : tc.impulse_locations_in_frames) {
    start_frame += impulse_start;
    for (size_t f = start_frame; f < start_frame + tc.impulse_width_in_frames; f++) {
      for (size_t c = 0; c < tc.input_format.channels(); c++) {
        input.samples()[input.SampleIndex(f, c)] = tc.impulse_magnitude;
      }
    }
  }

  // Render the input at a time such that the first frame of audio will be rendered into
  // the first frame of the ring buffer.
  auto packets = renderer->AppendPackets({&input});
  renderer->PlaySynchronized(this, device, 0);
  renderer->WaitForPackets(this, packets);

  // The ring buffer should contain the expected sequence of impulses.
  auto ring_buffer = device->SnapshotRingBuffer();
  size_t search_start_frame = 0;
  size_t search_end_frame = 0;
  for (size_t k = 0; k < tc.impulse_locations_in_frames.size(); k++) {
    // End this search halfway between impulses k and k+1.
    size_t input_next_midpoint_frame;
    if (k + 1 < tc.impulse_locations_in_frames.size()) {
      auto curr = impulse_start + tc.impulse_locations_in_frames[k];
      auto next = impulse_start + tc.impulse_locations_in_frames[k + 1];
      input_next_midpoint_frame = curr + (next - curr) / 2;
    } else {
      input_next_midpoint_frame = num_input_frames;
    }
    search_start_frame = search_end_frame;
    search_end_frame = input_frame_to_output_frame(input_next_midpoint_frame);

    // We expect zero noise in the output.
    constexpr auto kNoiseFloor = 0;

    // We expect to find this impulse at a precise frame.
    auto expected_output_frame =
        input_frame_to_output_frame(impulse_start + tc.impulse_locations_in_frames[k]);

    // Test each channel.
    for (size_t chan = 0; chan < tc.output_format.channels(); chan++) {
      SCOPED_TRACE(testing::Message() << "Channel " << chan);
      auto output_chan = AudioBufferSlice<OutputF>(&ring_buffer).GetChannel(chan);
      auto slice = AudioBufferSlice(&output_chan, search_start_frame, search_end_frame);
      auto output_frame = FindImpulseLeadingEdge(slice, kNoiseFloor);
      if (!output_frame) {
        ADD_FAILURE() << "Could not find impulse " << k << " in ring buffer\n"
                      << "Expected at ring buffer frame " << expected_output_frame << "\n"
                      << "Ring buffer is:";
        output_chan.Display(search_start_frame, search_end_frame);
        continue;
      }
      EXPECT_EQ(expected_output_frame, *output_frame + search_start_frame)
          << "Found impulse " << k << " at an unexpected location";
    }
  }

  if (flag_save_inputs_and_outputs) {
    WriteWavFile<InputF>(tc.test_name, "input", &input);
    WriteWavFile<OutputF>(tc.test_name, "ring_buffer", &ring_buffer);
  }
}

// Explicitly instantiate (almost) all possible implementations.
// We intentionally don't implementations with OutT = UNSIGNED_8 because no such hardware exists,
// therefore it's not worth testing.
#define INSTANTIATE(InT, OutT)                                  \
  template void HermeticGoldenTest::RunWaveformTest<InT, OutT>( \
      const WaveformTestCase<InT, OutT>& tc);                   \
  template void HermeticGoldenTest::RunImpulseTest<InT, OutT>(const ImpulseTestCase<InT, OutT>& tc);

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
