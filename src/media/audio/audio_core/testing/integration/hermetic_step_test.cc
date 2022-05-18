// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/testing/integration/hermetic_step_test.h"

#include <fuchsia/media/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/audio_core/testing/integration/renderer_shim.h"
#include "src/media/audio/lib/format/audio_buffer.h"
#include "src/media/audio/lib/wav/wav_writer.h"

using ASF = fuchsia::media::AudioSampleFormat;

namespace media::audio::test {

template <ASF InputFormat, ASF OutputFormat>
void HermeticStepTest::Run(const HermeticStepTest::TestCase<InputFormat, OutputFormat>& tc) {
  // Helper to translate from an input frame number to an output frame number.
  auto input_frame_to_output_frame = [&tc](int64_t input_frame) {
    auto input_fps = static_cast<double>(tc.input_format.frames_per_second());
    auto output_fps = static_cast<double>(tc.output_format.frames_per_second());
    return static_cast<int64_t>(
        std::ceil(output_fps / input_fps * static_cast<double>(input_frame)));
  };
  auto output_frame_to_input_frame = [&tc](int64_t output_frame) {
    auto input_fps = static_cast<double>(tc.input_format.frames_per_second());
    auto output_fps = static_cast<double>(tc.output_format.frames_per_second());
    return static_cast<int64_t>(
        std::ceil(input_fps / output_fps * static_cast<double>(output_frame)));
  };

  // Compute the number of input frames.
  auto output_step_pre_pad =
      static_cast<int64_t>(std::max(tc.pipeline.ramp_in_width, tc.pipeline.pos_filter_width));
  auto output_step_stabilization =
      static_cast<int64_t>(std::max(tc.pipeline.stabilization_width, tc.pipeline.neg_filter_width));
  auto output_step_destabilization = static_cast<int64_t>(
      std::max(tc.pipeline.destabilization_width, tc.pipeline.pos_filter_width));
  auto output_step_post_pad =
      static_cast<int64_t>(std::max(tc.pipeline.decay_width, tc.pipeline.neg_filter_width));

  auto input_step_pre_pad =
      std::max(output_step_pre_pad, output_frame_to_input_frame(output_step_pre_pad));
  auto input_step_stabilization =
      std::max(output_step_stabilization, output_frame_to_input_frame(output_step_stabilization));
  auto input_step_destabilization = std::max(
      output_step_destabilization, output_frame_to_input_frame(output_step_destabilization));
  auto input_step_post_pad =
      std::max(output_step_post_pad, output_frame_to_input_frame(output_step_post_pad));

  ASSERT_GT(tc.source_step_width_in_frames, input_step_stabilization + input_step_destabilization)
      << "Step width must be greater than the sum of stabilization widths "
      << input_step_stabilization << " and " << input_step_destabilization;
  auto output_step_width = input_frame_to_output_frame(tc.source_step_width_in_frames);

  auto num_input_frames = input_step_pre_pad + tc.source_step_width_in_frames + input_step_post_pad;
  auto num_output_frames = output_step_pre_pad + output_step_width + output_step_post_pad;
  num_output_frames = std::max(static_cast<int64_t>(AddSlackToOutputFrames(num_output_frames)),
                               tc.output_format.frames_per_second() / 2L);

  auto device = CreateOutput(AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS, tc.output_format,
                             num_output_frames, std::nullopt, tc.pipeline.output_device_gain_db);
  // Set the thermal state here as needed.
  if (tc.thermal_state.has_value()) {
    if (ConfigurePipelineForThermal(tc.thermal_state.value()) != ZX_OK) {
      return;
    }
  }

  auto renderer = CreateAudioRenderer(tc.input_format, num_input_frames);
  // Set the gain here as needed.
  if (tc.gain_db.has_value()) {
    renderer->SetGain(*tc.gain_db);
  }

  AudioBuffer<InputFormat> input_buffer(tc.input_format, num_input_frames);
  for (auto frame_num = input_step_pre_pad;
       frame_num < input_step_pre_pad + tc.source_step_width_in_frames; ++frame_num) {
    for (auto chan = 0; chan < tc.input_format.channels(); ++chan) {
      input_buffer.samples()[input_buffer.SampleIndex(frame_num, chan)] = tc.source_step_magnitude;
    }
  }

  // Render the input such that the first frame will be rendered into the ring buffer frame.
  auto packets = renderer->AppendPackets({&input_buffer});
  renderer->PlaySynchronized(this, device, 0);
  renderer->WaitForPackets(this, packets);

  auto ring_buffer = device->SnapshotRingBuffer();
  // If underflows occurred during our testing, SKIP (don't pass or fail).
  // TODO(fxbug.dev/80003): Remove workarounds when underflow conditions are fixed.
  if (DeviceHasUnderflows(device)) {
    GTEST_SKIP() << "Skipping step magnitude checks due to underflows";
    __builtin_unreachable();
  }

  // This fixture currently assesses only the magnitude (not the timing) of the step, when
  // "settled". Due to filter width, the step's leading edge may not be instantaneous, so we search
  // from buffer start and end toward the middle, finding the first frames with half the expected
  // magnitude, then advance inward by the stabilization widths to ensure we look at a fully
  // stabilized index. , then split the difference.
  for (auto chan = 0; chan < tc.output_format.channels(); ++chan) {
    auto output_chan_buffer = AudioBufferSlice<OutputFormat>(&ring_buffer).GetChannel(chan);

    int64_t leading_edge;
    for (leading_edge = 0; leading_edge < output_chan_buffer.NumFrames(); ++leading_edge) {
      auto value = std::abs(output_chan_buffer.samples()[leading_edge]);
      if (value >= std::abs(tc.expected_output_magnitude / 2)) {
        FX_LOGS(INFO) << "Found leading edge at [" << leading_edge << "] on value of " << value;
        break;
      }
    }

    int64_t trailing_edge;
    for (trailing_edge = ring_buffer.NumFrames() - 1; trailing_edge >= 0; --trailing_edge) {
      auto value = std::abs(output_chan_buffer.samples()[trailing_edge]);
      if (value >= std::abs(tc.expected_output_magnitude / 2)) {
        FX_LOGS(INFO) << "Found trailing edge at [" << trailing_edge << "] on value of " << value;
        break;
      }
    }

    auto display_buffer = [&output_chan_buffer, chan, output_step_pre_pad,
                           output_step_stabilization, output_step_width, output_step_post_pad]() {
      // If we have an error, display the relevant portions of the output buffer (not all the
      // pre-padding but just an additional stabilization period before the input signal starts).
      if (output_step_stabilization) {
        output_chan_buffer.Display(output_step_pre_pad - output_step_stabilization,
                                   output_step_pre_pad,
                                   fxl::StringPrintf("Channel %u ramp-in", chan));
      }
      output_chan_buffer.Display(output_step_pre_pad, output_step_pre_pad + output_step_width,
                                 fxl::StringPrintf("Channel %u step", chan));
      if (output_step_post_pad) {
        output_chan_buffer.Display(output_step_pre_pad + output_step_width,
                                   output_step_pre_pad + output_step_width + output_step_post_pad,
                                   fxl::StringPrintf("Channel %u ramp-out", chan));
      }
    };

    SCOPED_TRACE(testing::Message() << "Testing channel " << chan);
    if (leading_edge > trailing_edge) {
      display_buffer();
      ADD_FAILURE() << "Step edges not found";
      continue;
    }

    leading_edge += output_step_stabilization;
    FX_LOGS(INFO) << "Advancing leading edge past stabilization zone, to [" << leading_edge
                  << "], value " << output_chan_buffer.samples()[leading_edge];
    trailing_edge -= output_step_destabilization;
    FX_LOGS(INFO) << "Moving trailing edge past destabilization zone, to [" << trailing_edge
                  << "], value " << output_chan_buffer.samples()[trailing_edge];
    if (leading_edge > trailing_edge) {
      display_buffer();
      ADD_FAILURE() << "Step cannot be less wide than the pre+post stabilization periods";
      continue;
    }

    // Round up, since input_frame_to_output_frame() "ceil"s as well.
    auto middle_idx = (leading_edge + trailing_edge + 1) / 2;
    auto middle_value = output_chan_buffer.samples()[middle_idx];

    if (middle_value < tc.expected_output_magnitude - tc.output_magnitude_tolerance ||
        middle_value > tc.expected_output_magnitude + tc.output_magnitude_tolerance) {
      display_buffer();
      ADD_FAILURE() << "Channel " << chan << ", expected mid-step value in range ["
                    << tc.expected_output_magnitude - tc.output_magnitude_tolerance << ", "
                    << tc.expected_output_magnitude + tc.output_magnitude_tolerance
                    << "], actual was " << middle_value;
      continue;
    }
  }

  if (save_input_and_output_files_) {
    WriteWavFile<InputFormat>(tc.test_name, "input", &input_buffer);
    WriteWavFile<OutputFormat>(tc.test_name, "ring_buffer", &ring_buffer);
  }
}

// Explicitly instantiate all implementations except UNSIGNED_8 (hardware is no longer in use).
#define INSTANTIATE(InputFormat, OutputFormat)                    \
  template void HermeticStepTest::Run<InputFormat, OutputFormat>( \
      const TestCase<InputFormat, OutputFormat>& tc);

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
