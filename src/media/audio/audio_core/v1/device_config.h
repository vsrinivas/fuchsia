// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_DEVICE_CONFIG_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_DEVICE_CONFIG_H_

#include <fuchsia/media/cpp/fidl.h>
#include <zircon/device/audio.h>

#include <vector>

#include "src/media/audio/audio_core/v1/loudness_transform.h"
#include "src/media/audio/audio_core/v1/pipeline_config.h"
#include "src/media/audio/audio_core/v1/stream_usage.h"
#include "src/media/audio/audio_core/v1/volume_curve.h"

namespace media::audio {

class DeviceConfig {
 public:
  class DeviceProfile {
   public:
    explicit DeviceProfile(StreamUsageSet supported_usages, VolumeCurve volume_curve,
                           float driver_gain_db, float software_gain_db)
        : usage_support_set_(std::move(supported_usages)),
          volume_curve_(std::move(volume_curve)),
          loudness_transform_(std::make_shared<MappedLoudnessTransform>(volume_curve_)),
          driver_gain_db_(driver_gain_db),
          software_gain_db_(software_gain_db) {}

    virtual ~DeviceProfile() = default;

    virtual bool supports_usage(StreamUsage usage) const {
      return usage_support_set_.find(usage) != usage_support_set_.end();
    }

    const VolumeCurve& volume_curve() const { return volume_curve_; }

    virtual const std::shared_ptr<LoudnessTransform>& loudness_transform() const;

    StreamUsageSet supported_usages() const { return usage_support_set_; }

    float driver_gain_db() const { return driver_gain_db_; }
    float software_gain_db() const { return software_gain_db_; }

   private:
    StreamUsageSet usage_support_set_;
    VolumeCurve volume_curve_;
    std::shared_ptr<LoudnessTransform> loudness_transform_;
    float driver_gain_db_;
    float software_gain_db_;
  };

  // A routing profile for a device.
  class OutputDeviceProfile : public DeviceProfile {
   public:
    OutputDeviceProfile()
        : OutputDeviceProfile(true, StreamUsageSetFromRenderUsages(kFidlRenderUsages)) {}

    explicit OutputDeviceProfile(VolumeCurve volume_curve)
        : OutputDeviceProfile(true, StreamUsageSetFromRenderUsages(kFidlRenderUsages),
                              std::move(volume_curve), false, PipelineConfig::Default(), 0.0, 0.0) {
    }

    OutputDeviceProfile(bool eligible_for_loopback, StreamUsageSet supported_usages)
        : OutputDeviceProfile(eligible_for_loopback, supported_usages,
                              VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume),
                              false, PipelineConfig::Default(), 0.0, 0.0) {}

    OutputDeviceProfile(bool eligible_for_loopback, StreamUsageSet supported_usages,
                        VolumeCurve volume_curve, bool independent_volume_control,
                        PipelineConfig pipeline_config, float driver_gain_db,
                        float software_gain_db)
        : DeviceProfile(std::move(supported_usages), std::move(volume_curve), driver_gain_db,
                        software_gain_db),
          eligible_for_loopback_(eligible_for_loopback),
          independent_volume_control_(independent_volume_control),
          pipeline_config_(std::move(pipeline_config)) {}

    struct Parameters {
      std::optional<bool> eligible_for_loopback;
      std::optional<StreamUsageSet> supported_usages;
      std::optional<bool> independent_volume_control;
      std::optional<PipelineConfig> pipeline_config;
      std::optional<float> driver_gain_db;
      std::optional<float> software_gain_db;
      std::optional<VolumeCurve> volume_curve;
    };

    bool supports_usage(StreamUsage usage) const override {
      // Temporary, until configs stop specifying 'eligible_for_loopback'.
      if (usage == StreamUsage::WithCaptureUsage(CaptureUsage::LOOPBACK) &&
          eligible_for_loopback_) {
        return true;
      }
      return DeviceProfile::supports_usage(usage);
    }

    bool supports_usage(RenderUsage usage) const {
      return supports_usage(StreamUsage::WithRenderUsage(usage));
    }

    const std::shared_ptr<LoudnessTransform>& loudness_transform() const override;

    // Whether this device is eligible to be looped back to loopback capturers.
    bool eligible_for_loopback() const {
      return eligible_for_loopback_ ||
             supports_usage(StreamUsage::WithCaptureUsage(CaptureUsage::LOOPBACK));
    }

    // Whether this device has independent volume control, and should therefore
    // receive routed streams at unity gain.
    bool independent_volume_control() const { return independent_volume_control_; }

    const PipelineConfig& pipeline_config() const { return pipeline_config_; }

   private:
    const static std::shared_ptr<LoudnessTransform> kNoOpTransform;
    bool eligible_for_loopback_ = true;
    bool independent_volume_control_ = false;
    PipelineConfig pipeline_config_ = PipelineConfig::Default();
  };

  class InputDeviceProfile : public DeviceProfile {
   public:
    static constexpr uint32_t kDefaultRate = 48000;

    InputDeviceProfile() : InputDeviceProfile(kDefaultRate, 0.0, 0.0) {}

    InputDeviceProfile(uint32_t rate, float driver_gain_db, float software_gain_db)
        : InputDeviceProfile(rate, StreamUsageSetFromCaptureUsages(kFidlCaptureUsages),
                             VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume),
                             driver_gain_db, software_gain_db) {}

    InputDeviceProfile(uint32_t rate, StreamUsageSet supported_usages, VolumeCurve volume_curve,
                       float driver_gain_db, float software_gain_db)
        : DeviceProfile(std::move(supported_usages), std::move(volume_curve), driver_gain_db,
                        software_gain_db),
          rate_(rate) {}

    uint32_t rate() const { return rate_; }

   private:
    uint32_t rate_;
  };

  DeviceConfig() = default;

  DeviceConfig(std::vector<std::pair<std::vector<audio_stream_unique_id_t>, OutputDeviceProfile>>
                   output_device_profiles,
               std::optional<OutputDeviceProfile> default_output_device_profile,
               std::vector<std::pair<std::vector<audio_stream_unique_id_t>, InputDeviceProfile>>
                   input_device_profiles,
               std::optional<InputDeviceProfile> default_input_device_profile,
               VolumeCurve default_volume_curve =
                   VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume))
      : output_device_profiles_(std::move(output_device_profiles)),
        default_output_device_profile_(default_output_device_profile.value_or(
            OutputDeviceProfile(std::move(default_volume_curve)))),
        input_device_profiles_(std::move(input_device_profiles)),
        default_input_device_profile_(default_input_device_profile.value_or(InputDeviceProfile())) {
  }

  const OutputDeviceProfile& output_device_profile(const audio_stream_unique_id_t& id) const {
    return FindDeviceProfile(id, output_device_profiles_, default_output_device_profile_);
  }
  const OutputDeviceProfile& default_output_device_profile() const {
    return default_output_device_profile_;
  }
  void SetOutputDeviceProfile(const audio_stream_unique_id_t& id,
                              const OutputDeviceProfile& profile) {
    AddDeviceProfile(id, profile, output_device_profiles_);
  }

  const InputDeviceProfile& input_device_profile(const audio_stream_unique_id_t& id) const {
    return FindDeviceProfile(id, input_device_profiles_, default_input_device_profile_);
  }
  const InputDeviceProfile& default_input_device_profile() const {
    return default_input_device_profile_;
  }

  // Searches device profiles for an effect with the specified instance name. Returns a pointer
  // to the effect or nullptr if not found.
  const PipelineConfig::EffectV1* FindEffectV1(const std::string& instance_name) const;

 private:
  friend class ProcessConfigBuilder;

  template <typename Profile>
  static const Profile& FindDeviceProfile(
      const audio_stream_unique_id_t& id,
      const std::vector<std::pair<std::vector<audio_stream_unique_id_t>, Profile>>& profiles,
      const Profile& default_profile) {
    auto it = std::find_if(profiles.begin(), profiles.end(), [id](auto set) {
      for (const auto& other_id : set.first) {
        if (std::memcmp(id.data, other_id.data, sizeof(id.data)) == 0) {
          return true;
        }
      }
      return false;
    });

    return it != profiles.end() ? it->second : default_profile;
  }

  template <typename Profile>
  static void AddDeviceProfile(
      const audio_stream_unique_id_t& id, const Profile& profile,
      std::vector<std::pair<std::vector<audio_stream_unique_id_t>, Profile>>& profiles) {
    auto it = std::find_if(profiles.begin(), profiles.end(), [id](auto set) {
      for (const auto& other_id : set.first) {
        if (std::memcmp(id.data, other_id.data, sizeof(id.data)) == 0) {
          return true;
        }
      }
      return false;
    });

    if (it != profiles.end()) {
      it->second = std::move(profile);
    } else {
      profiles.emplace_back(std::vector<audio_stream_unique_id_t>{id}, profile);
    }
  }

  // Profiles for explicitly configured devices.
  std::vector<std::pair<std::vector<audio_stream_unique_id_t>, OutputDeviceProfile>>
      output_device_profiles_;

  // The device profile to apply to devices without an explicit profile.
  OutputDeviceProfile default_output_device_profile_;

  std::vector<std::pair<std::vector<audio_stream_unique_id_t>, InputDeviceProfile>>
      input_device_profiles_;
  InputDeviceProfile default_input_device_profile_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_DEVICE_CONFIG_H_
