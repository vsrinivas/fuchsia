// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_THERMAL_AGENT_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_THERMAL_AGENT_H_

#include <fuchsia/thermal/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

#include <unordered_map>
#include <vector>

#include "src/media/audio/audio_core/context.h"
#include "src/media/audio/audio_core/device_config.h"
#include "src/media/audio/audio_core/thermal_config.h"

namespace media::audio {

class ThermalAgent : public fuchsia::thermal::Actor {
 public:
  static std::unique_ptr<ThermalAgent> CreateAndServe(Context* context);

  using SetConfigCallback =
      fit::function<void(const std::string& target_name, const std::string& config)>;

  ThermalAgent(fuchsia::thermal::ControllerPtr thermal_controller,
               const ThermalConfig& thermal_config, const DeviceConfig& device_config,
               SetConfigCallback set_config_callback);

 private:
  // fuchsia::thermal::Agent implementation.
  void SetThermalState(uint32_t state, SetThermalStateCallback callback) override;

  fuchsia::thermal::ControllerPtr thermal_controller_;
  fidl::Binding<fuchsia::thermal::Actor> binding_;

  // A map from target name to vector of effect configurations, where the vector maps each thermal
  // state index to the configuration the targeted effect should be in for that state.
  std::unordered_map<std::string, std::vector<std::string>> targets_;

  uint32_t current_state_ = 0;
  SetConfigCallback set_config_callback_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_THERMAL_AGENT_H_
