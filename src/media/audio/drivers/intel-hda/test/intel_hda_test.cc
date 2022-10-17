// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <set>
#include <string>

#include <audio-proto-utils/format-utils.h>
#include <audio-utils/audio-device-stream.h>
#include <audio-utils/audio-input.h>
#include <audio-utils/audio-output.h>
#include <audio-utils/audio-stream.h>
#include <fbl/string.h>
#include <zxtest/zxtest.h>

#include "audio_test_tools.h"
#include "board_name.h"
#include "sample_count_sink.h"
#include "silence_generator.h"

namespace {
namespace audio_fidl = fuchsia_hardware_audio;
}  // namespace

namespace audio::intel_hda {
namespace {

using audio::utils::AudioDeviceStream;
using audio::utils::AudioOutput;

void CheckBasicStreamInfo(AudioDeviceStream* stream) {
  // Fetch manufacturer information, and ensure it is something other than
  // the empty string.
  zx::result<fbl::String> result = GetStreamConfigString(stream, AUDIO_STREAM_STR_ID_MANUFACTURER);
  ASSERT_TRUE(result.is_ok());
  auto& manufacturer = result.value();
  EXPECT_GT(manufacturer.length(), 0);

  // Fetch supported audio formats, and ensure it is non-empty with some number of channels.
  ASSERT_OK(stream->GetSupportedFormats([](const audio_fidl::wire::SupportedFormats& formats) {
    auto& pcm = formats.pcm_supported_formats();
    EXPECT_GT(pcm.channel_sets()[0].attributes().count(), 0);
  }));
}

TEST(IntelHda, BasicStreamInfo) {
  // Check outputs.
  for (const auto& path : GetSystemAudioDevices().outputs) {
    std::unique_ptr<AudioDeviceStream> stream = CreateAndOpenOutputStream(path.c_str());
    ASSERT_NOT_NULL(stream.get());
    CheckBasicStreamInfo(stream.get());
  }

  // Check inputs.
  for (const auto& path : GetSystemAudioDevices().inputs) {
    std::unique_ptr<AudioDeviceStream> stream = CreateAndOpenInputStream(path.c_str());
    ASSERT_NOT_NULL(stream.get());
    CheckBasicStreamInfo(stream.get());
  }
}

TEST(IntelHda, PlaySilence) {
  for (const auto& path : GetSystemAudioDevices().outputs) {
    // Open the stream.
    std::cerr << "Playing silence on device '" << path.c_str() << "'";
    std::unique_ptr<audio::utils::AudioOutput> output = CreateAndOpenOutputStream(path.c_str());

    // Set the output stream format.
    audio::utils::AudioStream::Format format;
    format.channels = 2;
    format.frame_rate = 48'000U;
    format.sample_format = AUDIO_SAMPLE_FORMAT_16BIT;
    SilenceGenerator silence_generator(format, /*duration_seconds=*/0.1);

    // Play silence.
    //
    // We can't verify that the data is being pumped out to the speaker,
    // but this exercises the DMA, ring buffers, etc etc.
    ASSERT_OK(output->Play(silence_generator));
  }
}

void TestAudioInputRecord(audio::utils::AudioInput* input) {
  // Set the output stream format.
  audio::utils::AudioStream::Format format;
  format.channels = 2;
  format.frame_rate = 48'000U;
  format.sample_format = AUDIO_SAMPLE_FORMAT_16BIT;
  const uint64_t channels_to_use = (1 << format.channels) - 1;  // Enable all.
  input->SetFormat(format.frame_rate, format.channels, channels_to_use, format.sample_format);

  // Record a small number of samples of audio.
  //
  // We don't attempt to verify the contents, but rather just exercise
  // DMA, ring buffers, etc.
  constexpr int kSamplesToCapture = 5'000;
  SampleCountSink sink{/*samples_to_capture=*/kSamplesToCapture};
  zx_status_t result = input->Record(sink, /*duration_seconds=*/10.0f);
  // We receive "ZX_ERR_STOP" if we received all of our samples.
  // If we get another error, something has gone wrong.
  EXPECT_EQ(result, ZX_ERR_STOP);
  EXPECT_GE(sink.total_samples(), kSamplesToCapture);
}

TEST(IntelHda, RecordData) {
  for (const auto& path : GetSystemAudioDevices().inputs) {
    // Open the stream.
    std::cerr << "Recording input from device '" << path.c_str() << "'";
    std::unique_ptr<audio::utils::AudioInput> input = CreateAndOpenInputStream(path.c_str());
    TestAudioInputRecord(input.get());
  }
}

}  // namespace
}  // namespace audio::intel_hda

int main(int argc, char** argv) {
  // Only run tests on systems that have Intel HDA hardware present.
  if (audio::intel_hda::GetSystemAudioDevices().controllers.empty()) {
    std::cerr << "No Intel HDA hardware found. Skipping tests.";
    return 0;
  }

  // Run tests.
  return RUN_ALL_TESTS(argc, argv);
}
