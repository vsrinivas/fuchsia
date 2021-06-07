// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/virtual_camera/stream_storage.h"

namespace camera {

void StreamStorage::SetStreamConfigAtIndex(size_t index,
                                           fuchsia::camera2::hal::StreamConfig stream_config) {
  if (stream_configs_.size() <= index) {
    stream_configs_.resize(index + 1);
  }

  stream_configs_[index] = std::move(stream_config);
}

fuchsia::camera2::hal::Config StreamStorage::GetConfig() {
  fuchsia::camera2::hal::Config config{};
  for (const auto& stream_config : stream_configs_) {
    if (stream_config) {
      config.stream_configs.push_back(fidl::Clone(stream_config.value()));
    }
  }
  return config;
}

}  // namespace camera
