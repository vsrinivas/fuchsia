// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/test/hermetic_impulse_test.h"

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

template <ASF InputFormat, ASF OutputFormat>
void HermeticImpulseTest::Run(const HermeticImpulseTest::TestCase<InputFormat, OutputFormat>& tc) {
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
  auto input_impulse_start = tc.pipeline.neg_filter_width;
  AudioBuffer<InputFormat> input(tc.input_format, num_input_frames);
  for (auto start_frame : tc.impulse_locations_in_frames) {
    start_frame += input_impulse_start;
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

  auto ring_buffer = device->SnapshotRingBuffer();

  // The ring buffer should contain the expected sequence of impulses.
  // Due to smoothing effects, the detected leading edge of each impulse might be offset
  // slightly from the expected location, however each impulse should be offset by the
  // same amount. Empirically, we see offsets as high as 0.5ms. Allow up to 1ms.
  ssize_t max_impulse_offset_frames = tc.output_format.frames_per_ns().Scale(zx::msec(1).get());
  std::unordered_map<size_t, ssize_t> first_impulse_offset_per_channel;
  size_t search_start_frame = 0;
  size_t search_end_frame = 0;

  for (size_t k = 0; k < tc.impulse_locations_in_frames.size(); k++) {
    // End this search halfway between impulses k and k+1.
    size_t input_next_midpoint_frame;
    if (k + 1 < tc.impulse_locations_in_frames.size()) {
      auto curr = input_impulse_start + tc.impulse_locations_in_frames[k];
      auto next = input_impulse_start + tc.impulse_locations_in_frames[k + 1];
      input_next_midpoint_frame = curr + (next - curr) / 2;
    } else {
      input_next_midpoint_frame = num_input_frames;
    }
    search_start_frame = search_end_frame;
    search_end_frame = input_frame_to_output_frame(input_next_midpoint_frame);

    // We expect zero noise in the output.
    constexpr auto kNoiseFloor = 0;

    // Impulse should be at this frame +/- max_impulse_offset_frames.
    auto expected_output_frame =
        input_frame_to_output_frame(input_impulse_start + tc.impulse_locations_in_frames[k]);

    // Test each channel.
    for (size_t chan = 0; chan < tc.output_format.channels(); chan++) {
      SCOPED_TRACE(testing::Message() << "Channel " << chan);
      auto output_chan = AudioBufferSlice<OutputFormat>(&ring_buffer).GetChannel(chan);
      auto slice = AudioBufferSlice(&output_chan, search_start_frame, search_end_frame);
      auto relative_output_frame = FindImpulseLeadingEdge(slice, kNoiseFloor);
      if (!relative_output_frame) {
        ADD_FAILURE() << "Could not find impulse " << k << " in ring buffer\n"
                      << "Expected at ring buffer frame " << expected_output_frame << "\n"
                      << "Ring buffer is:";
        output_chan.Display(search_start_frame, search_end_frame);
        continue;
      }
      auto output_frame = *relative_output_frame + search_start_frame;
      if (k == 0) {
        // First impulse decides the offset.
        auto offset =
            static_cast<ssize_t>(output_frame) - static_cast<ssize_t>(expected_output_frame);
        EXPECT_LE(std::abs(offset), max_impulse_offset_frames)
            << "Found impulse " << k << " at an unexpected location: at frame " << output_frame
            << ", expected within " << max_impulse_offset_frames << " frames of "
            << expected_output_frame;
        first_impulse_offset_per_channel[chan] = offset;
      } else {
        // Other impulses should have the same offset.
        auto expected_offset = first_impulse_offset_per_channel[chan];
        EXPECT_EQ(expected_output_frame + expected_offset, output_frame)
            << "Found impulse " << k << " at an unexpected location; expected_offset is "
            << expected_offset;
      }
    }
  }

  if (save_input_and_output_files_) {
    WriteWavFile<InputFormat>(tc.test_name, "input", &input);
    WriteWavFile<OutputFormat>(tc.test_name, "ring_buffer", &ring_buffer);
  }
}

// Explicitly instantiate (almost) all possible implementations.
// We intentionally don't instantiate implementations with OutputFormat = UNSIGNED_8
// because such hardware is no longer in use, therefore it's not worth testing.
#define INSTANTIATE(InputFormat, OutputFormat)                       \
  template void HermeticImpulseTest::Run<InputFormat, OutputFormat>( \
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
