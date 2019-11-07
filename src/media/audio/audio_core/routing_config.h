// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_ROUTING_CONFIG_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_ROUTING_CONFIG_H_

#include <fuchsia/media/cpp/fidl.h>
#include <zircon/device/audio.h>

#include <unordered_set>
#include <vector>

namespace media::audio {

class RoutingConfig {
 public:
  using UsageSupportSet = std::unordered_set<uint32_t>;

  bool DeviceSupportsOutputUsage(const audio_stream_unique_id_t& id,
                                 fuchsia::media::AudioRenderUsage usage) const;

 private:
  friend class ProcessConfigBuilder;

  // The usage support sets for explicitly configured devices.
  std::vector<std::pair<audio_stream_unique_id_t, UsageSupportSet>>
      device_output_usage_support_sets_;

  // The output usage support set to apply to devices without an explicit support set. If not
  // provided in the config, the behavior is to allow all usages for unrecognized devices.
  std::optional<UsageSupportSet> default_output_usage_support_set_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_ROUTING_CONFIG_H_
