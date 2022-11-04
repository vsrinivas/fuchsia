// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_PLUG_DETECTOR_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_PLUG_DETECTOR_H_

#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

#include <string>

#include "src/media/audio/audio_core/v1/audio_driver.h"

namespace media::audio {

class PlugDetector {
 public:
  static std::unique_ptr<PlugDetector> Create();

  virtual ~PlugDetector() = default;

  // Callback invoked whenever a new device is added to the system.
  using Observer = fit::function<void(
      std::string, bool, fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig>)>;
  virtual zx_status_t Start(Observer o) = 0;

  virtual void Stop() = 0;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_PLUG_DETECTOR_H_
