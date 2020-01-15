// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_ROUTING_CONFIG_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_ROUTING_CONFIG_H_

#include <fuchsia/media/cpp/fidl.h>
#include <zircon/device/audio.h>

#include <unordered_set>
#include <vector>

#include "src/media/audio/audio_core/loudness_transform.h"
#include "src/media/audio/audio_core/pipeline_config.h"

namespace media::audio {

class RoutingConfig {
 public:
  using UsageSupportSet = std::unordered_set<uint32_t>;

  // A routing profile for a device.
  class DeviceProfile {
   public:
    DeviceProfile() = default;

    DeviceProfile(bool eligible_for_loopback, UsageSupportSet output_usage_support_set,
                  bool independent_volume_control = false,
                  PipelineConfig pipeline_config = PipelineConfig::Default())
        : eligible_for_loopback_(eligible_for_loopback),
          independent_volume_control_(independent_volume_control),
          pipeline_config_(std::move(pipeline_config)),
          output_usage_support_set_(std::move(output_usage_support_set)) {}

    bool supports_usage(fuchsia::media::AudioRenderUsage usage) const {
      return !output_usage_support_set_ ||
             output_usage_support_set_->find(fidl::ToUnderlying(usage)) !=
                 output_usage_support_set_->end();
    }

    // Whether this device is eligible to be looped back to loopback capturers.
    bool eligible_for_loopback() const { return eligible_for_loopback_; }

    const std::shared_ptr<LoudnessTransform>& loudness_transform() const;

    // Whether this device has independent volume control, and should therefore
    // receive routed streams at unity gain.
    bool independent_volume_control() const { return independent_volume_control_; }

    const PipelineConfig& pipeline_config() const { return pipeline_config_; }

   private:
    const static std::shared_ptr<LoudnessTransform> kNoOpTransform;

    bool eligible_for_loopback_ = true;
    bool independent_volume_control_ = false;
    PipelineConfig pipeline_config_ = PipelineConfig::Default();

    // The set of output usages supported by the device.
    std::optional<UsageSupportSet> output_usage_support_set_;
  };

  RoutingConfig() {}

  RoutingConfig(std::vector<std::pair<audio_stream_unique_id_t, DeviceProfile>> device_profiles,
                std::optional<DeviceProfile> default_device_profile)
      : device_profiles_(std::move(device_profiles)),
        default_device_profile_(default_device_profile.value_or(DeviceProfile())) {}

  const DeviceProfile& device_profile(const audio_stream_unique_id_t& id) const {
    auto it = std::find_if(device_profiles_.begin(), device_profiles_.end(), [id](auto set) {
      return std::memcmp(id.data, set.first.data, sizeof(id.data)) == 0;
    });

    return it != device_profiles_.end() ? it->second : default_device_profile_;
  }

  const DeviceProfile& default_device_profile() const { return default_device_profile_; }

 private:
  friend class ProcessConfigBuilder;

  // Profiles for explicitly configured devices.
  std::vector<std::pair<audio_stream_unique_id_t, DeviceProfile>> device_profiles_;

  // The device profile to apply to devices without an explicit profile.
  DeviceProfile default_device_profile_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_ROUTING_CONFIG_H_
