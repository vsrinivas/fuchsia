// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/routing_config.h"

#include <algorithm>
#include <cstring>

namespace media::audio {

bool RoutingConfig::DeviceSupportsOutputUsage(const audio_stream_unique_id_t& id,
                                              fuchsia::media::AudioRenderUsage usage) const {
  auto usage_key = fidl::ToUnderlying(usage);
  auto it = std::find_if(
      device_output_usage_support_sets_.begin(), device_output_usage_support_sets_.end(),
      [id](auto set) { return std::memcmp(id.data, set.first.data, sizeof(id.data)) == 0; });
  if (it != device_output_usage_support_sets_.end()) {
    return it->second.find(usage_key) != it->second.end();
  }

  if (default_output_usage_support_set_) {
    return default_output_usage_support_set_->find(usage_key) !=
           default_output_usage_support_set_->end();
  }

  return true;
}

}  // namespace media::audio
