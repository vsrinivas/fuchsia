// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_PROCESS_CONFIG_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_PROCESS_CONFIG_H_

#include <optional>

#include "src/lib/syslog/cpp/logger.h"
#include "src/media/audio/audio_core/loudness_transform.h"
#include "src/media/audio/audio_core/routing_config.h"
#include "src/media/audio/audio_core/volume_curve.h"

namespace media::audio {

class ProcessConfig;

class ProcessConfigBuilder {
 public:
  ProcessConfigBuilder& SetDefaultVolumeCurve(VolumeCurve curve);
  ProcessConfigBuilder& AddDeviceRoutingProfile(
      std::pair<std::optional<audio_stream_unique_id_t>, RoutingConfig::DeviceProfile>
          keyed_profile);
  ProcessConfig Build();

 private:
  std::optional<VolumeCurve> default_volume_curve_;
  std::vector<std::pair<audio_stream_unique_id_t, RoutingConfig::DeviceProfile>> device_profiles_;
  std::optional<RoutingConfig::DeviceProfile> default_device_profile_;
};

class ProcessConfig {
 public:
  class HandleImpl {
   public:
    ~HandleImpl() { ProcessConfig::instance_ = {}; }

    // Disallow copy/move.
    HandleImpl(const HandleImpl&) = delete;
    HandleImpl& operator=(const HandleImpl&) = delete;
    HandleImpl(HandleImpl&& o) = delete;
    HandleImpl& operator=(HandleImpl&& o) = delete;

   private:
    friend class ProcessConfig;
    HandleImpl() = default;
  };
  using Handle = std::unique_ptr<HandleImpl>;

  // Sets the |ProcessConfig|.
  //
  // |ProcessConfig::instance()| will return a reference to |config| as long as the returned
  // |ProcessConfig::Handle| exists. It's illegal to call |set_instance| while a
  // |ProcessConfig::Handle| is active.
  [[nodiscard]] static ProcessConfig::Handle set_instance(ProcessConfig config) {
    FX_CHECK(!ProcessConfig::instance_);
    ProcessConfig::instance_ = {std::move(config)};
    return std::unique_ptr<HandleImpl>(new HandleImpl);
  }
  // Returns the |ProcessConfig|. Must be called while there is a
  static const ProcessConfig& instance() {
    FX_CHECK(ProcessConfig::instance_);
    return *ProcessConfig::instance_;
  }

  using Builder = ProcessConfigBuilder;
  ProcessConfig(VolumeCurve curve, RoutingConfig routing_config)
      : default_volume_curve_(std::move(curve)),
        default_loudness_transform_(
            std::make_shared<MappedLoudnessTransform>(default_volume_curve_)),
        routing_config_(std::move(routing_config)) {}

  const VolumeCurve& default_volume_curve() const { return default_volume_curve_; }
  const RoutingConfig& routing_config() const { return routing_config_; }
  const std::shared_ptr<LoudnessTransform>& default_loudness_transform() const {
    return default_loudness_transform_;
  }

 private:
  static std::optional<ProcessConfig> instance_;

  VolumeCurve default_volume_curve_;
  std::shared_ptr<LoudnessTransform> default_loudness_transform_;
  RoutingConfig routing_config_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_PROCESS_CONFIG_H_
