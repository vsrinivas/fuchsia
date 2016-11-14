// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

#include "apps/media/src/audio_server/audio_output.h"
#include "apps/media/src/audio_server/audio_output_manager.h"

namespace media {
namespace audio {

class UsbOutputEnum {
 public:
  UsbOutputEnum();

  ~UsbOutputEnum();

  AudioOutputPtr GetDefaultOutput(AudioOutputManager* manager);

 private:
  static const std::string kAudioDeviceClassPath;

  std::vector<std::string> output_device_paths_;
};

}  // namespace audio
}  // namespace media
