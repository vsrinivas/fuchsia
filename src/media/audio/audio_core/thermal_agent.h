// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_THERMAL_AGENT_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_THERMAL_AGENT_H_

#include <fuchsia/thermal/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

#include <vector>

#include "src/media/audio/audio_core/device_config.h"
#include "src/media/audio/audio_core/thermal_config.h"

namespace media::audio {

class ThermalAgent : public fuchsia::thermal::Actor {
 public:
  // Merged thermal policy for one target.
  class Target {
   public:
    Target(const std::string& name, std::vector<std::string> configs_by_state)
        : name_(name), configs_by_state_(configs_by_state) {}

    const std::string& name() const { return name_; }
    const std::vector<std::string>& configs_by_state() const { return configs_by_state_; }

   private:
    std::string name_;
    std::vector<std::string> configs_by_state_;
  };

  using SetConfigCallback =
      fit::function<void(const std::string& target_name, const std::string& config)>;

  ThermalAgent(fuchsia::thermal::ControllerPtr thermal_controller,
               const ThermalConfig& thermal_config, const DeviceConfig& device_config,
               SetConfigCallback set_config_callback);

 private:
  // fuchsia::thermal::Agent implementation.
  void SetThermalState(uint32_t state, SetThermalStateCallback callback) override;

  // Finds the nominal config string for the specified target. Returns no value if the specified
  // target could not be found.
  std::optional<std::string> FindNominalConfigForTarget(const std::string& target_name,
                                                        const DeviceConfig& device_config);

  fuchsia::thermal::ControllerPtr thermal_controller_;
  fidl::Binding<fuchsia::thermal::Actor> binding_;
  std::vector<Target> targets_;
  uint32_t current_state_ = 0;
  SetConfigCallback set_config_callback_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_THERMAL_AGENT_H_
