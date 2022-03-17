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
  // If tc.channels_to_test ever becomes mandatory (not optional), use it directly
  std::set<int32_t> chans_to_test;
  if (tc.channels_to_test.has_value()) {
    ASSERT_FALSE(tc.channels_to_test->empty()) << "channels_to_test is present but empty";
    chans_to_test = *tc.channels_to_test;
  } else {
    for (auto chan = 0; chan < tc.output_format.channels(); ++chan) {
      chans_to_test.insert(chan);
    }
  }

  // Compute the number of input frames.
  auto first_impulse = tc.impulse_locations_in_frames.front();
  auto first_impulse_to_last_impulse = tc.impulse_locations_in_frames.back();

  auto input_impulse_pre_pad = std::max(tc.pipeline.ramp_in_width, tc.pipeline.pos_filter_width);
  auto input_impulse_post_pad = std::max(tc.pipeline.decay_width, tc.pipeline.neg_filter_width);

  auto num_input_frames = input_impulse_pre_pad + first_impulse + first_impulse_to_last_impulse +
                          tc.impulse_width_in_frames + input_impulse_post_pad;

  // Helper to translate from an input frame number to an output frame number.
  auto input_frame_to_output_frame = [&tc](int64_t input_frame) {
    auto input_fps = static_cast<double>(tc.input_format.frames_per_second());
    auto output_fps = static_cast<double>(tc.output_format.frames_per_second());
    return static_cast<int64_t>(
        std::ceil(output_fps / input_fps * static_cast<double>(input_frame)));
  };

  auto num_output_frames = input_frame_to_output_frame(num_input_frames);
  auto device = CreateOutput(AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS, tc.output_format,
                             AddSlackToOutputFrames(num_output_frames), std::nullopt,
                             tc.pipeline.output_device_gain_db);
  auto renderer = CreateAudioRenderer(tc.input_format, num_input_frames);

  // Write all of the impulses to an input buffer so we can easily write the full
  // input to a WAV file for debugging. Include silence at the beginning to account
  // for ring in; this allows us to align the input and output WAV files.
  AudioBuffer<InputFormat> input(tc.input_format, num_input_frames);
  for (auto start_frame : tc.impulse_locations_in_frames) {
    start_frame += input_impulse_pre_pad;
    for (auto f = start_frame; f < start_frame + tc.impulse_width_in_frames; f++) {
      for (auto chan = 0; chan < tc.input_format.channels(); chan++) {
        input.samples()[input.SampleIndex(f, chan)] = tc.impulse_magnitude;
      }
    }
  }

  // Render the input at a time such that the first frame of audio will be rendered into
  // the first frame of the ring buffer.
  auto packets = renderer->AppendPackets({&input});
  renderer->PlaySynchronized(this, device, 0);
  renderer->WaitForPackets(this, packets);

  auto ring_buffer = device->SnapshotRingBuffer();
  // If underflows occurred during our testing, SKIP (don't pass or fail).
  // TODO(fxbug.dev/80003): Remove workarounds when underflow conditions are fixed.
  if (DeviceHasUnderflows(device)) {
    GTEST_SKIP() << "Skipping impulse position checks due to underflows";
    __builtin_unreachable();
  }

  // The ring buffer should contain the expected sequence of impulses.
  // Due to smoothing effects, the detected leading edge of each impulse might be offset
  // slightly from the expected location, however each impulse should be offset by the
  // same amount. Empirically, we see offsets as high as 0.5ms. Allow up to 1ms.
  int64_t max_impulse_offset_frames = tc.output_format.frames_per_ns().Scale(zx::msec(1).get());
  std::unordered_map<int32_t, int64_t> first_impulse_offset_per_channel;
  int64_t search_start_frame = 0;
  int64_t search_end_frame = 0;

  for (int64_t k = 0; k < static_cast<int64_t>(tc.impulse_locations_in_frames.size()); k++) {
    // End this search halfway between impulses k and k+1.
    int64_t input_next_midpoint_frame;
    if (k + 1 < static_cast<int64_t>(tc.impulse_locations_in_frames.size())) {
      auto curr = input_impulse_pre_pad + tc.impulse_locations_in_frames[k];
      auto next = input_impulse_pre_pad + tc.impulse_locations_in_frames[k + 1];
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
        input_frame_to_output_frame(input_impulse_pre_pad + tc.impulse_locations_in_frames[k]);

    // Test each channel.
    for (auto chan : chans_to_test) {
      ASSERT_GE(chan, 0);
      ASSERT_LT(chan, tc.output_format.channels());

      SCOPED_TRACE(testing::Message() << "Seeking an impulse in channel " << chan);
      auto output_chan = AudioBufferSlice<OutputFormat>(&ring_buffer).GetChannel(chan);
      auto slice = AudioBufferSlice(&output_chan, search_start_frame, search_end_frame);
      auto relative_output_frame = FindImpulseCenter(slice, kNoiseFloor);
      if (!relative_output_frame) {
        ADD_FAILURE() << "Could not find impulse #" << k << " in ring buffer\n"
                      << "Expected at frame index " << expected_output_frame << "\n"
                      << "Ring buffer is:";
        output_chan.Display(search_start_frame, search_end_frame);
        continue;
      }
      int64_t output_frame = *relative_output_frame + search_start_frame;
      if (k == 0) {
        // First impulse decides the offset.
        int64_t offset = output_frame - expected_output_frame;
        if (std::abs(offset) > max_impulse_offset_frames) {
          output_chan.Display(output_frame - 32, output_frame + 32,
                              fxl::StringPrintf("Chan %u, impulse %zd", chan, k));
          ADD_FAILURE() << "Found impulse #" << k << " (channel " << chan
                        << ") at unexpected frame index " << output_frame << ": expected within "
                        << max_impulse_offset_frames << " frames of " << expected_output_frame;
        }
        first_impulse_offset_per_channel[chan] = offset;
      } else {
        // Other impulses should have the same offset.
        auto expected_offset = first_impulse_offset_per_channel[chan];
        if (expected_output_frame + expected_offset != output_frame) {
          output_chan.Display(output_frame - 32, output_frame + 32,
                              fxl::StringPrintf("Chan %u, impulse %zd", chan, k));
          ADD_FAILURE() << "Found impulse #" << k << " (channel " << chan
                        << ") at unexpected frame index " << output_frame
                        << "; expected at frame index " << expected_output_frame + expected_offset;
        }
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
