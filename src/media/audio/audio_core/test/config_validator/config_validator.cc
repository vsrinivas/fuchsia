// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/media/audio/audio_core/process_config_loader.h"

namespace media::audio {
namespace {

constexpr char kConfigsDirectory[] = "/pkg/audio_core_config";

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

}  // namespace
}  // namespace media::audio
