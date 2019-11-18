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

  // A routing profile for a device.
  class DeviceProfile {
   public:
    DeviceProfile() = default;

    DeviceProfile(bool eligible_for_loopback, UsageSupportSet output_usage_support_set)
        : eligible_for_loopback_(eligible_for_loopback),
          output_usage_support_set_(std::move(output_usage_support_set)) {}

    bool supports_usage(fuchsia::media::AudioRenderUsage usage) const {
      return !output_usage_support_set_ ||
             output_usage_support_set_->find(fidl::ToUnderlying(usage)) !=
                 output_usage_support_set_->end();
    }

    bool eligible_for_loopback() const { return eligible_for_loopback_; }

   private:
    // Whether this device is eligible to be looped back to loopback capturers.
    bool eligible_for_loopback_ = true;
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

 private:
  friend class ProcessConfigBuilder;

  // Profiles for explicitly configured devices.
  std::vector<std::pair<audio_stream_unique_id_t, DeviceProfile>> device_profiles_;

  // The device profile to apply to devices without an explicit profile.
  DeviceProfile default_device_profile_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_ROUTING_CONFIG_H_
