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
#include "silence_generator.h"

namespace audio::intel_hda {
namespace {

using audio::utils::AudioDeviceStream;
using audio::utils::AudioOutput;

void CheckBasicStreamInfo(AudioDeviceStream* stream) {
  // Fetch manufacturer information, and ensure it is something other than
  // the empty string.
  StatusOr<fbl::String> result = GetStreamConfigString(stream, AUDIO_STREAM_STR_ID_MANUFACTURER);
  ASSERT_TRUE(result.ok());
  EXPECT_GT(result.ValueOrDie().length(), 0);

  // Fetch supported audio formats, and ensure it is non-empty.
  fbl::Vector<audio_stream_format_range_t> formats;
  zx_status_t status = stream->GetSupportedFormats(&formats);
  ASSERT_OK(status);
  EXPECT_GT(formats.size(), 0);
}

TEST(IntelHda, BasicStreamInfo) {
  // Check outputs.
  for (const auto& path : GetSystemAudioDevices().outputs) {
    fbl::unique_ptr<AudioDeviceStream> stream = CreateAndOpenOutputStream(path.c_str());
    ASSERT_NOT_NULL(stream.get());
    CheckBasicStreamInfo(stream.get());
  }

  // Check inputs.
  for (const auto& path : GetSystemAudioDevices().inputs) {
    fbl::unique_ptr<AudioDeviceStream> stream = CreateAndOpenInputStream(path.c_str());
    ASSERT_NOT_NULL(stream.get());
    CheckBasicStreamInfo(stream.get());
  }
}

TEST(IntelHda, PlaySilence) {
  for (const auto& path : GetSystemAudioDevices().outputs) {
    // Open the stream.
    std::cerr << "Playing silence on device '" << path.c_str() << "'\n";
    fbl::unique_ptr<audio::utils::AudioOutput> output = CreateAndOpenOutputStream(path.c_str());

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

}  // namespace
}  // namespace audio::intel_hda

int main(int argc, char** argv) {
  // Only run tests on systems that have Intel HDA hardware present.
  if (audio::intel_hda::GetSystemAudioDevices().controllers.empty()) {
    std::cerr << "No Intel HDA hardware found. Skipping tests.\n";
    return 0;
  }

  // Run tests.
  return RUN_ALL_TESTS(argc, argv);
}
