// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_POLICY_LOADER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_POLICY_LOADER_H_

#include <fuchsia/media/cpp/fidl.h>

#include <rapidjson/document.h>

#include "src/lib/fxl/logging.h"

namespace media {
namespace audio {

class AudioAdmin;

class PolicyLoader {
 public:
  // Utility Methods
  static std::optional<fuchsia::media::AudioRenderUsage> JsonToRenderUsage(
      const rapidjson::Value& usage);
  static std::optional<fuchsia::media::AudioCaptureUsage> JsonToCaptureUsage(
      const rapidjson::Value& usage);
  static std::optional<fuchsia::media::Behavior> JsonToBehavior(const rapidjson::Value& usage);

  static std::optional<rapidjson::Document> ParseConfig(const char* file_body);

  static void LoadDefaults(AudioAdmin* audio_admin);

 private:
  static zx_status_t LoadConfig(AudioAdmin* audio_admin, const char* file_body);
  static zx_status_t LoadConfigFromFile(AudioAdmin* audio_admin, const std::string config);
};

}  // namespace audio
}  // namespace media

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_POLICY_LOADER_H_
