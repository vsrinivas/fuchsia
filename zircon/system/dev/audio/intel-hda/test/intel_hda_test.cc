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

#include <audio-utils/audio-device-stream.h>
#include <audio-utils/audio-input.h>
#include <audio-utils/audio-output.h>
#include <fbl/string.h>
#include <zxtest/zxtest.h>

#include "audio_test_tools.h"
#include "board_name.h"

namespace audio::intel_hda {
namespace {

using audio::utils::AudioOutput;

TEST(IntelHda, BasicStreamInfo) {
  for (const auto& path : GetSystemAudioDevices().inputs) {
    // Open the selected stream.
    fbl::unique_ptr<AudioOutput> stream = AudioOutput::Create(path.c_str());
    ASSERT_NOT_NULL(stream.get());
    zx_status_t status = stream->Open();
    ASSERT_OK(status);

    // Fetch manufacturer information, and ensure it is something other than
    // the empty string.
    StatusOr<fbl::String> result =
        GetStreamConfigString(stream.get(), AUDIO_STREAM_STR_ID_MANUFACTURER);
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.ValueOrDie().length(), 0);

    // Fetch supported audio formats, and ensure it is non-empty.
    fbl::Vector<audio_stream_format_range_t> formats;
    status = stream->GetSupportedFormats(&formats);
    ASSERT_OK(status);
    EXPECT_GT(formats.size(), 0);
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
