// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fpromise/result.h>

#include <gtest/gtest.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/media/audio/audio_core/v1/policy_loader.h"
#include "src/media/audio/audio_core/v1/process_config_loader.h"

namespace media::audio {
namespace {

constexpr char kConfigsDirectory[] = "/pkg/audio_core_config";
constexpr char kPolicyConfigsDirectory[] = "/pkg/audio_policy";

TEST(ConfigValidator, LoadAudioCoreConfig) {
  std::vector<std::string> configs;
  files::ReadDirContents(kConfigsDirectory, &configs);
  for (const auto& filename : configs) {
    auto config_path = files::JoinPath(kConfigsDirectory, filename);
    if (!files::IsFile(config_path)) {
      continue;
    }

    SCOPED_TRACE(filename);
    auto process_config = ProcessConfigLoader::LoadProcessConfig(config_path.c_str());
    if (!process_config.is_ok()) {
      ADD_FAILURE() << process_config.error();
    }
  }
}

TEST(ConfigValidator, LoadAudioPolicyConfig) {
  std::vector<std::string> configs;
  files::ReadDirContents(kPolicyConfigsDirectory, &configs);
  for (const auto& filename : configs) {
    if (filename == ".") {
      continue;
    }
    auto config_path = files::JoinPath(kPolicyConfigsDirectory, filename);
    if (!files::IsFile(config_path)) {
      ADD_FAILURE() << "Audio policy file '" << config_path << "': IsFile error";
      continue;
    }

    SCOPED_TRACE(filename);
    auto result = PolicyLoader::LoadConfigFromFile(config_path.c_str());
    if (!result.is_ok()) {
      std::string error_msg;
      switch (result.error()) {
        case ZX_ERR_NOT_FOUND:
          error_msg = "Audio policy file '" + config_path + "': not found";
          break;
        case ZX_ERR_NOT_SUPPORTED:
          error_msg = "Audio policy file '" + config_path + "': did not obey the JSON schema";
          break;
        default:
          error_msg = "Audio policy file '" + config_path + "': other file error (" +
                      std::to_string(result.error()) + ")";
          break;
      }
      ADD_FAILURE() << error_msg;
    }
  }
}

}  // namespace
}  // namespace media::audio
