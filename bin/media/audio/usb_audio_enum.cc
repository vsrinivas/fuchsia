// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/audio/usb_audio_enum.h"

#include <sstream>

#include <fcntl.h>
#include <dirent.h>
#include <magenta/device/audio.h>

#include "lib/ftl/files/eintr_wrapper.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/logging.h"

namespace media {

// static
const std::string UsbAudioEnum::kAudioDeviceClassPath = "/dev/class/audio";

UsbAudioEnum::UsbAudioEnum() {
  DIR* dir = opendir(kAudioDeviceClassPath.c_str());
  if (dir == nullptr) {
    FTL_DLOG(ERROR) << "Couldn't open audio device class directory "
                    << kAudioDeviceClassPath;
    return;
  }

  for (struct dirent* entry = readdir(dir); entry != nullptr;
       entry = readdir(dir)) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    std::ostringstream device_path_stream;
    device_path_stream << kAudioDeviceClassPath << "/" << entry->d_name;
    const std::string& device_path = device_path_stream.str();

    ftl::UniqueFD fd(open(device_path.c_str(), O_RDWR));
    if (!fd.is_valid()) {
      FTL_DLOG(WARNING) << "Failed to open audio device " << device_path;
      continue;
    }

    int device_type;
    int result = ioctl_audio_get_device_type(fd.get(), &device_type);
    if (result != sizeof(device_type)) {
      FTL_DLOG(WARNING) << "Failed to get device type for " << device_path;
      continue;
    }

    if (device_type == AUDIO_TYPE_SOURCE) {
      FTL_DLOG(INFO) << "Enumerated input device " << device_path;
      input_device_paths_.push_back(device_path);
    } else if (device_type == AUDIO_TYPE_SINK) {
      FTL_DLOG(INFO) << "Enumerated output device " << device_path;
      output_device_paths_.push_back(device_path);
    } else {
      FTL_DLOG(WARNING) << "Unrecognized device type " << device_type;
    }
  }

  closedir(dir);
}

UsbAudioEnum::~UsbAudioEnum() {}

}  // namespace media
