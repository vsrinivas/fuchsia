// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_DEVICE_CONFIG_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_DEVICE_CONFIG_H_

#include <fuchsia/media/cpp/fidl.h>
#include <zircon/device/audio.h>

#include <vector>

#include "src/media/audio/audio_core/loudness_transform.h"
#include "src/media/audio/audio_core/pipeline_config.h"
#include "src/media/audio/audio_core/stream_usage.h"

namespace media::audio {

class DeviceConfig {
 public:
  // A routing profile for a device.
  class OutputDeviceProfile {
   public:
    OutputDeviceProfile() = default;

    OutputDeviceProfile(bool eligible_for_loopback, RenderUsageSet output_usage_support_set,
                        bool independent_volume_control = false,
                        PipelineConfig pipeline_config = PipelineConfig::Default())
        : eligible_for_loopback_(eligible_for_loopback),
          independent_volume_control_(independent_volume_control),
          pipeline_config_(std::move(pipeline_config)),
          output_usage_support_set_(std::move(output_usage_support_set)) {}

    bool supports_usage(RenderUsage usage) const {
      return !output_usage_support_set_ ||
             output_usage_support_set_->find(usage) != output_usage_support_set_->end();
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
    std::optional<RenderUsageSet> output_usage_support_set_;
  };

  class InputDeviceProfile {
   public:
    static constexpr uint32_t kDefaultRate = 48000;

    InputDeviceProfile() : InputDeviceProfile(kDefaultRate) {}

    InputDeviceProfile(uint32_t rate) : rate_(rate) {}

    uint32_t rate() const { return rate_; }

   private:
    uint32_t rate_;
  };

  DeviceConfig() {}

  DeviceConfig(
      std::vector<std::pair<audio_stream_unique_id_t, OutputDeviceProfile>> output_device_profiles,
      std::optional<OutputDeviceProfile> default_output_device_profile,
      std::vector<std::pair<audio_stream_unique_id_t, InputDeviceProfile>> input_device_profiles,
      std::optional<InputDeviceProfile> default_input_device_profile)
      : output_device_profiles_(std::move(output_device_profiles)),
        default_output_device_profile_(
            default_output_device_profile.value_or(OutputDeviceProfile())),
        input_device_profiles_(std::move(input_device_profiles)),
        default_input_device_profile_(default_input_device_profile.value_or(InputDeviceProfile())) {
  }

  const OutputDeviceProfile& output_device_profile(const audio_stream_unique_id_t& id) const {
    return DeviceProfile(id, output_device_profiles_, default_output_device_profile_);
  }
  const OutputDeviceProfile& default_output_device_profile() const {
    return default_output_device_profile_;
  }

  const InputDeviceProfile& input_device_profile(const audio_stream_unique_id_t& id) const {
    return DeviceProfile(id, input_device_profiles_, default_input_device_profile_);
  }
  const InputDeviceProfile& default_input_device_profile() const {
    return default_input_device_profile_;
  }

  // Searches device profiles for an effect with the specified instance name. Returns a pointer
  // to the effect or nullptr if not found.
  const PipelineConfig::Effect* FindEffect(const std::string& instance_name) const;

 private:
  friend class ProcessConfigBuilder;

  template <typename Profile>
  static const Profile& DeviceProfile(
      const audio_stream_unique_id_t& id,
      const std::vector<std::pair<audio_stream_unique_id_t, Profile>>& profiles,
      const Profile& default_profile) {
    auto it = std::find_if(profiles.begin(), profiles.end(), [id](auto set) {
      return std::memcmp(id.data, set.first.data, sizeof(id.data)) == 0;
    });

    return it != profiles.end() ? it->second : default_profile;
  }

  // Profiles for explicitly configured devices.
  std::vector<std::pair<audio_stream_unique_id_t, OutputDeviceProfile>> output_device_profiles_;

  // The device profile to apply to devices without an explicit profile.
  OutputDeviceProfile default_output_device_profile_;

  std::vector<std::pair<audio_stream_unique_id_t, InputDeviceProfile>> input_device_profiles_;
  InputDeviceProfile default_input_device_profile_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_DEVICE_CONFIG_H_
