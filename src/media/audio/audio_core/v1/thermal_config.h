// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_THERMAL_CONFIG_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_THERMAL_CONFIG_H_

#include <fuchsia/thermal/cpp/fidl.h>

#include <vector>

namespace media::audio {

// Represents the thermal policy configuration found in an audio_core configuration file.
//
// Changing the thermal state for audio might require multiple effects to be updated. Here, each
// State represents a set of effects configurations. Each EffectConfig specifies how a specific
// named effect should be configured, when changed to that thermal state. The normal (unthrottled)
// State is designated by `kNominalThermalState`.
class ThermalConfig {
 public:
  static constexpr uint64_t kNominalThermalState = 0;

  explicit operator bool() const { return !states_.empty(); }

  class EffectConfig {
   public:
    EffectConfig(const char* name, const char* config_string)
        : name_(name), config_string_(config_string) {}

    const std::string& name() const { return name_; }
    const std::string& config_string() const { return config_string_; }

   private:
    std::string name_;
    std::string config_string_;
  };

  class State {
   public:
    State(uint64_t thermal_state_number, std::vector<EffectConfig> effect_configs)
        : thermal_state_number_(thermal_state_number), effect_configs_(std::move(effect_configs)) {}

    uint64_t thermal_state_number() const { return thermal_state_number_; }
    const std::vector<EffectConfig>& effect_configs() const { return effect_configs_; }

   private:
    uint64_t thermal_state_number_;
    std::vector<EffectConfig> effect_configs_;
  };

  explicit ThermalConfig(std::vector<State> states) : states_(std::move(states)) {}

  const std::vector<State>& states() const { return states_; }

 private:
  friend class ProcessConfigBuilder;

  std::vector<State> states_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_THERMAL_CONFIG_H_
