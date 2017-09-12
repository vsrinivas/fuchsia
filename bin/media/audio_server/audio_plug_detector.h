// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/device/vfs.h>
#include <magenta/types.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>

#include "lib/media/fidl/media_result.fidl.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fsl/io/device_watcher.h"

namespace media {
namespace audio {

class AudioOutputManager;

class AudioPlugDetector {
 public:
  AudioPlugDetector() {}
  ~AudioPlugDetector();

  MediaResult Start(AudioOutputManager* manager);
  void Stop();

 private:
  void AddAudioDevice(int dir_fd, const std::string& name);
  std::unique_ptr<fsl::DeviceWatcher> watcher_;
  AudioOutputManager* manager_ = nullptr;
};

}  // namespace audio
}  // namespace media
