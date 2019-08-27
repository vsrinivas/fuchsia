// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_SETTINGS_JSON_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_SETTINGS_JSON_H_

#include <zircon/types.h>

#include <rapidjson/schema.h>

namespace media::audio {

class AudioDeviceSettings;

class AudioDeviceSettingsJson {
 public:
  // Creates |AudioDeviceSettingsJson| with the default schema.
  static zx_status_t Create(std::unique_ptr<AudioDeviceSettingsJson>* ptr);

  // Creates |AudioDeviceSettingsJson| with a custom schema.
  static zx_status_t CreateWithSchema(const char* schema,
                                      std::unique_ptr<AudioDeviceSettingsJson>* ptr);

  // Disallow move and copy.
  AudioDeviceSettingsJson(AudioDeviceSettingsJson&& o) noexcept = delete;
  AudioDeviceSettingsJson& operator=(AudioDeviceSettingsJson&& o) noexcept = delete;
  AudioDeviceSettingsJson(const AudioDeviceSettingsJson&) = delete;
  AudioDeviceSettingsJson& operator=(const AudioDeviceSettingsJson&) = delete;

  zx_status_t Deserialize(int fd, AudioDeviceSettings* settings);
  zx_status_t Serialize(int fd, const AudioDeviceSettings& settings);

 private:
  explicit AudioDeviceSettingsJson(rapidjson::SchemaDocument schema) : schema_(std::move(schema)) {}

  rapidjson::SchemaDocument schema_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_SETTINGS_JSON_H_
