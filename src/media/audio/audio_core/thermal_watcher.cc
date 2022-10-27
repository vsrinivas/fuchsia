// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/thermal_watcher.h"

#include <lib/async/dispatcher.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

#include <rapidjson/document.h>

#include "fuchsia/thermal/cpp/fidl.h"
#include "src/media/audio/audio_core/audio_device_manager.h"
#include "src/media/audio/audio_core/reporter.h"
#include "src/media/audio/audio_core/thermal_config.h"

namespace media::audio {

// Thermal Client
//
// static
std::unique_ptr<ThermalWatcher> ThermalWatcher::CreateAndWatch(Context& context) {
  if (!context.process_config().thermal_config()) {
    FX_LOGS(WARNING) << "No thermal configuration, so we won't start the thermal watcher";
    return nullptr;
  }

  auto connector =
      context.component_context().svc()->Connect<fuchsia::thermal::ClientStateConnector>();
  fuchsia::thermal::ClientStateWatcherPtr watcher_request;
  connector->Connect(
      "audio", watcher_request.NewRequest(context.threading_model().FidlDomain().dispatcher()));

  struct AccessToPrivateCtor : public ThermalWatcher {
    AccessToPrivateCtor(fuchsia::thermal::ClientStateWatcherPtr state_watcher, Context& context)
        : ThermalWatcher(std::move(state_watcher), context) {}
  };
  auto thermal_watcher = std::make_unique<AccessToPrivateCtor>(std::move(watcher_request), context);

  thermal_watcher->WatchThermalState();

  return thermal_watcher;
}

ThermalWatcher::ThermalWatcher(fuchsia::thermal::ClientStateWatcherPtr state_watcher,
                               Context& context)
    : watcher_(std::move(state_watcher)),
      dispatcher_(context.threading_model().FidlDomain().dispatcher()),
      context_(context),
      thermal_state_(0) {
  FX_CHECK(watcher_);

  auto thermal_config = context_.process_config().thermal_config();
  if (thermal_config.states().empty()) {
    FX_LOGS(ERROR) << "No thermal states, so we won't start the thermal watcher";
    watcher_ = nullptr;
    return;
  }

  watcher_.set_error_handler([this](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Connection to fuchsia.thermal.ClientStateWatcher failed: ";
    watcher_.set_error_handler(nullptr);
    watcher_.Unbind();
  });
}

void ThermalWatcher::WatchThermalState() {
  if (!watcher_.is_bound()) {
    return;
  }

  watcher_->Watch([this](uint64_t new_thermal_state) {
    SetThermalState(new_thermal_state);
    this->WatchThermalState();
  });
}

void ThermalWatcher::SetThermalState(uint64_t state) {
  if (thermal_state_ == state) {
    if constexpr (kLogThermalStateChanges) {
      FX_LOGS(INFO) << "No thermal state change (was already " << state << ")";
    }
    return;
  }
  for (auto& state_entry : context_.process_config().thermal_config().states()) {
    if (state_entry.thermal_state_number() == state) {
      for (auto& effect_config : state_entry.effect_configs()) {
        async::PostTask(
            context_.threading_model().FidlDomain().dispatcher(),
            [this, name = effect_config.name(), target_config = effect_config.config_string(),
             state]() {
              this->context_.effects_controller()->UpdateEffect(
                  name, target_config,
                  [instance = name, config = target_config,
                   state](fuchsia::media::audio::EffectsController_UpdateEffect_Result result) {
                    if (result.is_err()) {
                      std::ostringstream err;
                      if (result.err() == fuchsia::media::audio::UpdateEffectError::NOT_FOUND) {
                        err << "effect with name '" << instance << "' was not found";
                      } else {
                        err << "message '" << config << "' was rejected";
                      }
                      if constexpr (kLogThermalEffectEnumeration) {
                        FX_LOGS(ERROR) << "Unable to apply thermal policy: " << err.str();
                      } else {
                        FX_LOGS_FIRST_N(ERROR, 500)
                            << "Unable to apply thermal policy: " << err.str();
                      }
                    } else {
                      std::ostringstream out;
                      out << "Successfully updated effect '" << instance << "' for state " << state
                          << " with config '" << config << "'";
                      if constexpr (kLogThermalEffectEnumeration) {
                        FX_LOGS(INFO) << out.str();
                      } else {
                        FX_LOGS(DEBUG) << out.str();
                      }
                    }
                  });
            });
      }
      break;
    }
  }

  auto previous_state = thermal_state_;
  thermal_state_ = state;
  Reporter::Singleton().SetThermalState(state);
  if constexpr (kLogThermalStateChanges) {
    FX_LOGS(INFO) << "Thermal state change (from " << previous_state << " to " << thermal_state_
                  << ") has been posted";
  }
}

}  // namespace media::audio
