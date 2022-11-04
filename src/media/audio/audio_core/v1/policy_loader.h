// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_POLICY_LOADER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_POLICY_LOADER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fpromise/result.h>

#include <rapidjson/document.h>

#include "src/media/audio/audio_core/v1/audio_policy.h"

namespace media::audio {

class PolicyLoader {
 public:
  static AudioPolicy LoadPolicy();

  static fpromise::result<AudioPolicy, zx_status_t> LoadConfigFromFile(std::string filename);
  static fpromise::result<AudioPolicy> ParseConfig(const char* file_body);

 private:
  static bool ParseIdlePowerOptions(rapidjson::Document& doc,
                                    AudioPolicy::IdlePowerOptions& options);
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_POLICY_LOADER_H_
