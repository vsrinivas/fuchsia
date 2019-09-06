// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_SETTINGS_SERIALIZATION_IMPL_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_SETTINGS_SERIALIZATION_IMPL_H_

#include <zircon/types.h>

#include <rapidjson/schema.h>

#include "src/media/audio/audio_core/audio_device_settings_serialization.h"

namespace media::audio {

class AudioDeviceSettings;

class AudioDeviceSettingsSerializationImpl : public AudioDeviceSettingsSerialization {
 public:
  // Creates |AudioDeviceSettingsSerializationImpl| with the default schema.
  static zx_status_t Create(std::unique_ptr<AudioDeviceSettingsSerialization>* ptr);

  // Creates |AudioDeviceSettingsSerializationImpl| with a custom schema.
  static zx_status_t CreateWithSchema(const char* schema,
                                      std::unique_ptr<AudioDeviceSettingsSerialization>* ptr);

  // Disallow move and copy.
  AudioDeviceSettingsSerializationImpl(AudioDeviceSettingsSerializationImpl&& o) noexcept = delete;
  AudioDeviceSettingsSerializationImpl& operator=(
      AudioDeviceSettingsSerializationImpl&& o) noexcept = delete;
  AudioDeviceSettingsSerializationImpl(const AudioDeviceSettingsSerializationImpl&) = delete;
  AudioDeviceSettingsSerializationImpl& operator=(const AudioDeviceSettingsSerializationImpl&) =
      delete;

  zx_status_t Deserialize(int fd, AudioDeviceSettings* settings) final;
  zx_status_t Serialize(int fd, const AudioDeviceSettings& settings) final;

 private:
  explicit AudioDeviceSettingsSerializationImpl(rapidjson::SchemaDocument schema)
      : schema_(std::move(schema)) {}

  rapidjson::SchemaDocument schema_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_SETTINGS_SERIALIZATION_IMPL_H_
