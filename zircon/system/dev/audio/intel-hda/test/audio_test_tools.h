// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/types.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include <audio-utils/audio-device-stream.h>
#include <audio-utils/audio-input.h>
#include <audio-utils/audio-output.h>
#include <fbl/string.h>
#include <intel-hda/utils/status_or.h>

namespace audio::intel_hda {

// Audio devices present in the system.
struct SystemAudioDevices {
  // Audio inputs, e.g., "/dev/class/audio-input/000".
  std::vector<fbl::String> inputs;

  // Audio outputs, e.g., "/dev/class/audio-output/000".
  std::vector<fbl::String> outputs;

  // HDA controllers, e.g., "/dev/class/intel-hda/000".
  std::vector<fbl::String> controllers;
};

// Get the audio devices present in the system.
SystemAudioDevices GetSystemAudioDevices();

// Create and open an input/output audio device at |device|.
//
// Return nullptr on error.
fbl::unique_ptr<audio::utils::AudioOutput> CreateAndOpenOutputStream(const char* device);
fbl::unique_ptr<audio::utils::AudioInput> CreateAndOpenInputStream(const char* device);

// Fetch the string |id| from the given audio stream / device node.
StatusOr<fbl::String> GetStreamConfigString(audio::utils::AudioDeviceStream* stream,
                                            audio_stream_string_id_t id);

}  // namespace audio::intel_hda
