// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/device/vfs.h>
#include <magenta/types.h>
#include <mxtl/macros.h>
#include <mxtl/mutex.h>

#include "apps/media/services/media_result.fidl.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/mtl/io/device_watcher.h"

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
  enum class DevNodeType { AUDIO, AUDIO2_OUTPUT };

  void AddAudioDevice(int dir_fd, const std::string& name, DevNodeType type);
  std::vector<std::unique_ptr<mtl::DeviceWatcher>> watchers_;
  AudioOutputManager* manager_;
};

}  // namespace audio
}  // namespace media
