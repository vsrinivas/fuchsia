// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/thermal_agent.h"

#include <set>

#include "src/lib/syslog/cpp/logger.h"
#include "src/media/audio/audio_core/audio_device_manager.h"

namespace media::audio {
namespace {

std::vector<std::string> ConfigsByState(const std::vector<ThermalConfig::State>& config_states,
                                        const std::string& nominal_config,
                                        const std::vector<uint32_t>& trip_points) {
  std::vector<std::string> result;
  // The number of states is one greater than the number of trip points.
  result.reserve(trip_points.size() + 1);

  // Push the nominal config as the config for state 0.
  result.push_back(nominal_config);

  // Push the rest of the state configs. Configured states correspond to one or more merged states.
  auto config_state_iter = config_states.begin();
  for (auto trip_point : trip_points) {
    // If there are no more config states, just copy the previous config.
    if (config_state_iter != config_states.end()) {
      auto& config_state = *config_state_iter;

      // If we haven't hit the trip point of the current config state, just copy the previous
      // config.
      if (config_state.trip_point() == trip_point) {
        // We've reached the trip point of the current config state. Use its config.
        result.push_back(config_state.config());
        ++config_state_iter;
        continue;
      }
    }

    result.push_back(result.back());
  }

  return result;
}

}  // namespace

// static
std::unique_ptr<ThermalAgent> ThermalAgent::CreateAndServe(Context* context) {
  auto& thermal_config = context->process_config().thermal_config();
  if (thermal_config.entries().empty()) {
    // No thermal config so we don't start the thermal agent.
    return nullptr;
  }
  return std::make_unique<ThermalAgent>(
      context->component_context().svc()->Connect<fuchsia::thermal::Controller>(), thermal_config,
      context->process_config().device_config(),
      [context](const std::string& target_name, const std::string& config) {
        context->device_manager().SetEffectConfig(target_name, config);
      });
}

ThermalAgent::ThermalAgent(fuchsia::thermal::ControllerPtr thermal_controller,
                           const ThermalConfig& thermal_config, const DeviceConfig& device_config,
                           SetConfigCallback set_config_callback)
    : thermal_controller_(std::move(thermal_controller)),
      binding_(this),
      set_config_callback_(std::move(set_config_callback)) {
  FX_DCHECK(thermal_controller_);
  FX_DCHECK(set_config_callback_);

  if (thermal_config.entries().empty()) {
    // No thermal config. Nothing to do.
    thermal_controller_ = nullptr;
    return;
  }

  std::set<uint32_t> trip_points_set;
  for (auto& entry : thermal_config.entries()) {
    for (auto& state : entry.states()) {
      trip_points_set.insert(state.trip_point());
    }
  }

  std::vector<uint32_t> trip_points(trip_points_set.begin(), trip_points_set.end());

  for (auto& entry : thermal_config.entries()) {
    auto nominal_config = FindNominalConfigForTarget(entry.target_name(), device_config);
    if (nominal_config.has_value()) {
      targets_.emplace_back(entry.target_name(),
                            ConfigsByState(entry.states(), nominal_config.value(), trip_points));
    } else {
      FX_LOGS(ERROR) << "Thermal config references unknown target '" << entry.target_name() << "'.";
    }
  }

  thermal_controller_.set_error_handler([this](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Connection to fuchsia.thermal.Controller failed: ";
    thermal_controller_.set_error_handler(nullptr);
    thermal_controller_.Unbind();
  });

  thermal_controller_->Subscribe(
      binding_.NewBinding(), fuchsia::thermal::ActorType::AUDIO, std::move(trip_points),
      [this](fuchsia::thermal::Controller_Subscribe_Result result) {
        if (result.is_err()) {
          FX_CHECK(result.err() != fuchsia::thermal::Error::INVALID_ARGUMENTS);
          FX_LOGS(ERROR) << "fuchsia.thermal.Controller/Subscribe failed";
        }

        thermal_controller_.set_error_handler(nullptr);
        thermal_controller_.Unbind();
      });
}

void ThermalAgent::SetThermalState(uint32_t state, SetThermalStateCallback callback) {
  if (current_state_ != state) {
    for (auto& target : targets_) {
      FX_CHECK(state < target.configs_by_state().size());
      FX_CHECK(current_state_ < target.configs_by_state().size());
      if (target.configs_by_state()[state] != target.configs_by_state()[current_state_]) {
        set_config_callback_(target.name(), target.configs_by_state()[state]);
      }
    }

    current_state_ = state;
  }

  callback();
}

std::optional<std::string> ThermalAgent::FindNominalConfigForTarget(
    const std::string& target_name, const DeviceConfig& device_config) {
  // For 'special' target names (not effect names), this method must return a string. An empty
  // string is fine. The remainder of this method assumes the |target_name| references an effect.

  const PipelineConfig::Effect* effect = device_config.FindEffect(target_name);
  return effect ? std::optional(effect->effect_config) : std::nullopt;
}

}  // namespace media::audio
