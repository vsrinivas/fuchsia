// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_PLUG_DETECTOR_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_PLUG_DETECTOR_H_

#include <lib/fit/function.h>
#include <zircon/types.h>

#include <memory>
#include <string>
#include <vector>

#include "lib/fsl/io/device_watcher.h"

namespace media::audio {

class AudioPlugDetector {
 public:
  // Callback invoked whenever a new device is added to the system.
  using Observer = fit::function<void(zx::channel, std::string, bool)>;

  explicit AudioPlugDetector(Observer o) : observer_(std::move(o)) {}

  zx_status_t Start();
  void Stop();

 private:
  void AddAudioDevice(int dir_fd, const std::string& name, bool is_input);

  Observer observer_;
  std::vector<std::unique_ptr<fsl::DeviceWatcher>> watchers_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_PLUG_DETECTOR_H_
