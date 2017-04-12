// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

#include "apps/media/src/audio/usb_audio_enum.h"
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
  UsbAudioEnum usb_audio_enum_;
};

}  // namespace audio
}  // namespace media
