// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <zircon/compiler.h>
#include <zircon/device/vfs.h>
#include <zircon/types.h>

#include <media/cpp/fidl.h>
#include "lib/fsl/io/device_watcher.h"
#include "lib/fxl/files/unique_fd.h"

namespace media {
namespace audio {

class AudioDeviceManager;

class AudioPlugDetector {
 public:
  AudioPlugDetector() {}
  ~AudioPlugDetector();

  MediaResult Start(AudioDeviceManager* manager);
  void Stop();

 private:
  void AddAudioDevice(int dir_fd, const std::string& name, bool is_input);
  std::vector<std::unique_ptr<fsl::DeviceWatcher>> watchers_;
  AudioDeviceManager* manager_ = nullptr;
};

}  // namespace audio
}  // namespace media
