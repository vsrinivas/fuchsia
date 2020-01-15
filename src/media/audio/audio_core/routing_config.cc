// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/routing_config.h"

#include "src/media/audio/audio_core/process_config.h"

namespace media::audio {

const std::shared_ptr<LoudnessTransform> RoutingConfig::DeviceProfile::kNoOpTransform =
    std::make_shared<NoOpLoudnessTransform>();

const std::shared_ptr<LoudnessTransform>& RoutingConfig::DeviceProfile::loudness_transform() const {
  if (independent_volume_control_) {
    return kNoOpTransform;
  }

  return ProcessConfig::instance().default_loudness_transform();
}

}  // namespace media::audio
