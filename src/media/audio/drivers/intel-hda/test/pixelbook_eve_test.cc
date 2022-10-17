// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests for audio on the Pixelbook Eve (2017 Q4)
//
// While the generic HDA tests exercise any input / output streams it
// can find, these tests assume a particular topology and fail if we
// fail to meet that. This helps catch errors where the audio drivers
// are failing to expose all the expected interfaces. If we only test
// the interfaces exposed, we wouldn't notice something was wrong.

#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
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

using audio::utils::AudioDeviceStream;
using audio::utils::AudioInput;
using audio::utils::AudioOutput;

fbl::String GetDeviceName(AudioDeviceStream* stream) {
  zx::result<fbl::String> result = GetStreamConfigString(stream, AUDIO_STREAM_STR_ID_PRODUCT);
  if (!result.is_ok()) {
    return "<error>";
  }
  return result.value();
}

TEST(PixelbookEveAudio, Topology) {
  SystemAudioDevices devices = GetSystemAudioDevices();

  // Expect a single input, output, and controller.
  ASSERT_EQ(devices.inputs.size(), 1);
  ASSERT_EQ(devices.outputs.size(), 2);
  ASSERT_EQ(devices.controllers.size(), 1);

  // Ensure we have a microphone.
  {
    std::unique_ptr<AudioInput> input = AudioInput::Create(devices.inputs.at(0).c_str());
    ASSERT_OK(input->Open());
    ASSERT_NOT_NULL(input.get());
    ASSERT_EQ(GetDeviceName(input.get()), "Builtin Microphone");
  }

  // Ensure we have speakers.
  {
    std::unique_ptr<AudioOutput> output = AudioOutput::Create(devices.outputs.at(0).c_str());
    ASSERT_OK(output->Open());
    ASSERT_NOT_NULL(output.get());
    ASSERT_EQ(GetDeviceName(output.get()), "Builtin Speakers");
  }

  // Ensure we have headphone output.
  {
    std::unique_ptr<AudioOutput> output = AudioOutput::Create(devices.outputs.at(1).c_str());
    ASSERT_OK(output->Open());
    ASSERT_NOT_NULL(output.get());
    ASSERT_EQ(GetDeviceName(output.get()), "Builtin Headphone Jack");
  }
}

}  // namespace
}  // namespace audio::intel_hda

int main(int argc, char** argv) {
  // Only run tests on the Eve.
  fbl::String board_name;
  zx_status_t status = audio::intel_hda::GetBoardName(&board_name);
  if (status != ZX_OK) {
    std::cerr << "Unable to determine hardware platform: " << zx_status_get_string(status) << ".";
    return status;
  }
  if (board_name != "Eve") {
    std::cerr << "Skipping tests on unsupported platform '" << board_name.c_str() << "'.";
    return 0;
  }

  // Run tests.
  return RUN_ALL_TESTS(argc, argv);
}
