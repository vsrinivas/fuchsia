// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio/audio_input_enum.h"

#include <audio-utils/audio-input.h>
#include <dirent.h>
#include <fcntl.h>
#include <sstream>
#include <zircon/device/audio.h>

#include "lib/fxl/files/eintr_wrapper.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"

namespace media {

// static
const std::string AudioInputEnum::kAudioInputDeviceClassPath =
    "/dev/class/audio-input";

AudioInputEnum::AudioInputEnum() {
  DIR* dir = opendir(kAudioInputDeviceClassPath.c_str());
  if (dir == nullptr) {
    // If the audio input class path does not currently exist, it means that no
    // audio input devices have currently been published.
    return;
  }

  for (struct dirent* entry = readdir(dir); entry != nullptr;
       entry = readdir(dir)) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    std::ostringstream device_path_stream;
    device_path_stream << kAudioInputDeviceClassPath << "/" << entry->d_name;
    const std::string& device_path = device_path_stream.str();
    auto device = audio::utils::AudioInput::Create(device_path.c_str());
    zx_status_t res = device->Open();

    if (res != ZX_OK) {
      FXL_LOG(WARNING) << "Failed to open audio device " << device_path
                       << " (res " << res << ")";
      continue;
    }

    audio_stream_cmd_plug_detect_resp_t plug_state;
    res = device->GetPlugState(&plug_state);
    if (res != ZX_OK) {
      FXL_LOG(WARNING) << "Failed to get plug state for " << device_path
                       << " (res " << res << ")";
      continue;
    }

    if (!(plug_state.flags & AUDIO_PDNF_PLUGGED)) {
      plug_state.plug_state_time = 0;
    }

    FXL_DLOG(INFO) << "Enumerated input device " << device_path;
    input_devices_.emplace_back(std::move(device_path),
                                plug_state.plug_state_time);
  }

  closedir(dir);
}

AudioInputEnum::~AudioInputEnum() {}

}  // namespace media
