// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/thermal_agent.h"

#include <lib/syslog/cpp/macros.h>

#include <unordered_set>

#include "src/media/audio/audio_core/audio_device_manager.h"

namespace media::audio {
namespace {

// Finds the nominal config string for the specified target. Returns no value if the specified
// target could not be found.
std::optional<std::string> FindNominalConfigForTarget(const std::string& target_name,
                                                      const DeviceConfig& device_config) {
  // For 'special' target names (not effect names), this method must return a string. An empty
  // string is fine. The remainder of this method assumes the |target_name| references an effect.

  const PipelineConfig::Effect* effect = device_config.FindEffect(target_name);
  return effect ? std::optional(effect->effect_config) : std::nullopt;
}

// Constructs a map {target_name: configs_by_thermal_state}, where configs_by_thermal_state
// is a vector of configurations for the target indexed by thermal state.
std::unordered_map<std::string, std::vector<std::string>> PopulateTargetConfigurations(
    const ThermalConfig& thermal_config, const DeviceConfig& device_config) {
  const auto& entries = thermal_config.entries();
  const size_t num_thermal_states = entries.size() + 1;
  std::unordered_map<std::string, std::vector<std::string>> result;

  // "Bad" targets have no nominal configuration. We record them so the name of every such target
  // can be logged only once.
  std::unordered_set<std::string> bad_targets;

  for (size_t i = 0; i < entries.size(); i++) {
    const auto& entry = entries[i];

    for (const auto& transition : entry.state_transitions()) {
      const auto& target_name = transition.target_name();
      if (bad_targets.find(target_name) != bad_targets.end()) {
        continue;
      }

      auto configs_it = result.find(target_name);

      // This target isn't in target_configurations. If there's no corresponding nominal config,
      // record it as a bad target and continue. Otherwise, initialize this target's entry in
      // `result`.
      if (configs_it == result.end()) {
        auto nominal_config = FindNominalConfigForTarget(target_name, device_config);
        if (!nominal_config.has_value()) {
          bad_targets.insert(target_name);
          FX_LOGS(ERROR) << "Thermal config references unknown target '" << target_name << "'.";
          continue;
        }

        configs_it = result.insert({target_name, {}}).first;
        auto& configs = configs_it->second;
        configs.reserve(num_thermal_states);
        configs.push_back(nominal_config.value());
      }

      // `transition` specifies that this target should change from its previous configuration at
      // state `i` to `transition.config()` at state `i+1`. Copy the last element until entry `i`
      // is populated, and then copy the new config into position `i+1`.
      std::vector<std::string>& configs = configs_it->second;
      for (size_t j = configs.size(); j < i + 1; j++) {
        configs.push_back(configs.back());
      }
      configs.push_back(transition.config());
    }
  }

  // Extend the configs for each target to the appropriate length -- any target not present in the
  // final state transition will have missing elements.
  for (auto& entry : result) {
    auto& configs = entry.second;
    if (configs.size() < num_thermal_states) {
      for (size_t j = configs.size(); j < num_thermal_states + 1; j++) {
        configs.push_back(configs.back());
      }
    }
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
        auto promise = context->device_manager().UpdateEffect(target_name, config);
        context->threading_model().FidlDomain().executor()->schedule_task(
            promise.then([target_name, config](
                             fit::result<void, fuchsia::media::audio::UpdateEffectError>& result) {
              if (result.is_error()) {
                std::ostringstream err;
                if (result.error() == fuchsia::media::audio::UpdateEffectError::NOT_FOUND) {
                  err << "effect with name " << target_name << " was not found";
                } else {
                  err << "message " << config << " was rejected";
                }
                FX_LOGS_FIRST_N(ERROR, 10) << "Unable to apply thermal policy: " << err.str();
              }
            }));
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

  targets_ = PopulateTargetConfigurations(thermal_config, device_config);

  thermal_controller_.set_error_handler([this](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Connection to fuchsia.thermal.Controller failed: ";
    thermal_controller_.set_error_handler(nullptr);
    thermal_controller_.Unbind();
  });

  std::vector<fuchsia::thermal::TripPoint> trip_points;
  trip_points.reserve(thermal_config.entries().size());
  for (const auto& entry : thermal_config.entries()) {
    trip_points.push_back(entry.trip_point());
  }

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
    for (auto& [target_name, configs_by_state] : targets_) {
      FX_CHECK(state < configs_by_state.size());
      FX_CHECK(current_state_ < configs_by_state.size());
      if (configs_by_state[state] != configs_by_state[current_state_]) {
        set_config_callback_(target_name, configs_by_state[state]);
      }
    }

    current_state_ = state;
  }

  callback();
}

}  // namespace media::audio
