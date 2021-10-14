// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/pipeline_config.h"

namespace media::audio {

int16_t PipelineConfig::channels() const {
  // The bottommost effect that defines output_channels will define our channelization.
  for (auto it = root_.effects_v1.rbegin(); it != root_.effects_v1.rend(); ++it) {
    if (it->output_channels) {
      return *it->output_channels;
    }
  }

  // If no effect performs rechannelization, then our channelization is determined by the mix stage
  // itself.
  return root_.output_channels;
}

}  // namespace media::audio
