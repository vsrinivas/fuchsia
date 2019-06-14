// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_PLUG_DETECTOR_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_PLUG_DETECTOR_H_

#include <memory>
#include <string>
#include <vector>

#include "lib/fsl/io/device_watcher.h"

namespace media::audio {

class AudioDeviceManager;

class AudioPlugDetector {
 public:
  AudioPlugDetector() {}
  ~AudioPlugDetector();

  zx_status_t Start(AudioDeviceManager* manager);
  void Stop();

 private:
  void AddAudioDevice(int dir_fd, const std::string& name, bool is_input);
  std::vector<std::unique_ptr<fsl::DeviceWatcher>> watchers_;
  AudioDeviceManager* manager_ = nullptr;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_PLUG_DETECTOR_H_
