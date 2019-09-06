// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_SETTINGS_SERIALIZATION_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_SETTINGS_SERIALIZATION_H_

#include <zircon/types.h>

namespace media::audio {

class AudioDeviceSettings;

class AudioDeviceSettingsSerialization {
 public:
  virtual ~AudioDeviceSettingsSerialization() = default;
  virtual zx_status_t Deserialize(int fd, AudioDeviceSettings* settings) = 0;
  virtual zx_status_t Serialize(int fd, const AudioDeviceSettings& settings) = 0;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_SETTINGS_SERIALIZATION_H_
