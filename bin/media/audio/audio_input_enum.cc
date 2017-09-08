// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio/audio_input_enum.h"

#include <sstream>

#include <fcntl.h>
#include <dirent.h>

#include "lib/ftl/files/eintr_wrapper.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/logging.h"

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

    ftl::UniqueFD fd(open(device_path.c_str(), O_RDWR));
    if (!fd.is_valid()) {
      FTL_DLOG(WARNING) << "Failed to open audio device " << device_path;
      continue;
    }

    FTL_DLOG(INFO) << "Enumerated input device " << device_path;
    input_device_paths_.push_back(device_path);
  }

  closedir(dir);
}

AudioInputEnum::~AudioInputEnum() {}

}  // namespace media
