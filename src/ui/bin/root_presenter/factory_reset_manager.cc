// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/factory_reset_manager.h"

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/async/time.h>
#include <zircon/assert.h>
#include <zircon/status.h>

namespace root_presenter {

using fuchsia::media::AudioRenderUsage;
using fuchsia::media::sounds::Player_AddSoundFromFile_Result;
using fuchsia::media::sounds::Player_PlaySound_Result;

constexpr uint32_t FACTORY_RESET_SOUND_ID = 0;

FactoryResetManager::WatchHandler::WatchHandler(
    const fuchsia::recovery::ui::FactoryResetCountdownState& state) {
  state.Clone(&current_state_);
}

void FactoryResetManager::WatchHandler::Watch(WatchCallback callback) {
  hanging_get_ = std::move(callback);
  SendIfChanged();
}

void FactoryResetManager::WatchHandler::OnStateChange(
    const fuchsia::recovery::ui::FactoryResetCountdownState& state) {
  state.Clone(&current_state_);
  last_state_sent_ = false;
  SendIfChanged();
}

void FactoryResetManager::WatchHandler::SendIfChanged() {
  if (hanging_get_ && !last_state_sent_) {
    fuchsia::recovery::ui::FactoryResetCountdownState state_to_send;
    current_state_.Clone(&state_to_send);
    hanging_get_(std::move(state_to_send));
    last_state_sent_ = true;
    hanging_get_ = nullptr;
  }
}

FactoryResetManager::FactoryResetManager(sys::ComponentContext& context,
                                         std::shared_ptr<MediaRetriever> media_retriever)
    : media_retriever_(media_retriever) {
  context.outgoing()->AddPublicService<fuchsia::recovery::ui::FactoryResetCountdown>(
      [this](fidl::InterfaceRequest<fuchsia::recovery::ui::FactoryResetCountdown> request) {
        auto handler = std::make_unique<WatchHandler>(State());
        countdown_bindings_.AddBinding(std::move(handler), std::move(request));
      });
  context.outgoing()->AddPublicService<fuchsia::recovery::policy::Device>(
      [this](fidl::InterfaceRequest<fuchsia::recovery::policy::Device> request) {
        policy_bindings_.AddBinding(this, std::move(request));
      });

  context.svc()->Connect(factory_reset_.NewRequest());
  FX_DCHECK(factory_reset_);
  context.svc()->Connect(sound_player_.NewRequest());
  FX_DCHECK(sound_player_);
}

bool FactoryResetManager::OnMediaButtonReport(
    const fuchsia::ui::input::MediaButtonsReport& report) {
  switch (factory_reset_state_) {
    case FactoryResetState::ALLOWED: {
      return HandleReportOnAllowedState(report);
    }
    case FactoryResetState::DISALLOWED: {
      return HandleReportOnDisallowedState(report);
    }
    case FactoryResetState::BUTTON_COUNTDOWN: {
      return HandleReportOnButtonCountdown(report);
    }
    case FactoryResetState::RESET_COUNTDOWN: {
      return HandleReportOnResetCountdown(report);
    }
    default: {
      return false;
    }
  }
}

void FactoryResetManager::PlayCompleteSoundThenReset() {
  FX_LOGS(DEBUG) << "Playing countdown complete sound";
  factory_reset_state_ = FactoryResetState::TRIGGER_RESET;

  MediaRetriever::ResetSoundResult result = media_retriever_->GetResetSound();
  if (result.is_error()) {
    FX_LOGS(INFO) << "Skipping countdown complete sound. Unable to open audio file: "
                  << zx_status_get_string(result.error());
    TriggerFactoryReset();
    return;
  }

  sound_player_->AddSoundFromFile(
      FACTORY_RESET_SOUND_ID, std::move(result.value()),
      [this](Player_AddSoundFromFile_Result result) {
        if (result.is_response()) {
          sound_player_->PlaySound(FACTORY_RESET_SOUND_ID, AudioRenderUsage::SYSTEM_AGENT,
                                   [this](Player_PlaySound_Result result) {
                                     if (result.is_err()) {
                                       FX_LOGS(WARNING)
                                           << "Failed to play countdown complete sound in player";
                                     } else {
                                       sound_player_->RemoveSound(FACTORY_RESET_SOUND_ID);
                                     }

                                     // Trigger reset after sound completes, otherwise sound
                                     // is cut off. Reset regardless of whether the sound
                                     // played successfully or not.
                                     TriggerFactoryReset();
                                   });
        } else {
          FX_LOGS(WARNING) << "Failed to add countdown complete sound to player";
          // If we couldn't add the sound, don't bother trying to play
          // the sound, just trigger the reset early.
          TriggerFactoryReset();
        }
      });
}

void FactoryResetManager::TriggerFactoryReset() {
  FX_LOGS(WARNING) << "Triggering factory reset";
  FX_DCHECK(factory_reset_);
  factory_reset_->Reset([](zx_status_t status) {
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Factory service failed with status: " << zx_status_get_string(status);
    }
  });
}

void FactoryResetManager::NotifyStateChange() {
  for (auto& binding : countdown_bindings_.bindings()) {
    if (binding->is_bound()) {
      binding->impl()->OnStateChange(State());
    }
  }
}

fuchsia::recovery::ui::FactoryResetCountdownState FactoryResetManager::State() const {
  fuchsia::recovery::ui::FactoryResetCountdownState state;
  if (factory_reset_state_ == FactoryResetState::RESET_COUNTDOWN) {
    state.set_scheduled_reset_time(deadline_);
  }
  return state;
}

bool FactoryResetManager::HandleReportOnAllowedState(
    const fuchsia::ui::input::MediaButtonsReport& report) {
  if (!report.reset) {
    return false;
  }

  factory_reset_state_ = FactoryResetState::BUTTON_COUNTDOWN;
  start_reset_countdown_after_timeout_.Reset(
      fit::bind_member(this, &FactoryResetManager::StartFactoryResetCountdown));
  async::PostDelayedTask(async_get_default_dispatcher(),
                         start_reset_countdown_after_timeout_.callback(), kButtonCountdownDuration);
  return true;
}

bool FactoryResetManager::HandleReportOnDisallowedState(
    const fuchsia::ui::input::MediaButtonsReport& report) {
  return report.reset;
}

bool FactoryResetManager::HandleReportOnButtonCountdown(
    const fuchsia::ui::input::MediaButtonsReport& report) {
  FX_DCHECK(factory_reset_state_ != FactoryResetState::DISALLOWED)
      << "HandleReportOnButtonCountdown should not be called when on DISALLOWED state.";

  // If the reset button is no longer held, cancel the button countdown. Otherwise, ignore the
  // report.
  if (!report.reset) {
    start_reset_countdown_after_timeout_.Cancel();
    factory_reset_state_ = FactoryResetState::ALLOWED;
  }

  return true;
}

bool FactoryResetManager::HandleReportOnResetCountdown(
    const fuchsia::ui::input::MediaButtonsReport& report) {
  FX_DCHECK(factory_reset_state_ != FactoryResetState::DISALLOWED)
      << "HandleReportOnResetCountdown should not be called when on DISALLOWED state.";

  // If the reset button is no longer held, cancel the reset countdown and notify the state change.
  // Otherwise, ignore the report.
  if (!report.reset) {
    FX_LOGS(WARNING) << "Factory reset canceled";
    reset_after_timeout_.Cancel();
    factory_reset_state_ = FactoryResetState::ALLOWED;
    deadline_ = ZX_TIME_INFINITE_PAST;
    NotifyStateChange();
  }

  return true;
}

void FactoryResetManager::StartFactoryResetCountdown() {
  if (factory_reset_state_ == FactoryResetState::RESET_COUNTDOWN) {
    return;
  }

  FX_LOGS(WARNING) << "Starting factory reset countdown";
  factory_reset_state_ = FactoryResetState::RESET_COUNTDOWN;
  deadline_ = async_now(async_get_default_dispatcher()) + kResetCountdownDuration.get();
  NotifyStateChange();

  reset_after_timeout_.Reset(
      fit::bind_member(this, &FactoryResetManager::PlayCompleteSoundThenReset));
  async::PostDelayedTask(async_get_default_dispatcher(), reset_after_timeout_.callback(),
                         kResetCountdownDuration);
}

void FactoryResetManager::SetIsLocalResetAllowed(bool allowed) {
  if (factory_reset_state_ == FactoryResetState::DISALLOWED && allowed) {
    // If Factory reset was disallowed, and if the new policy is allowed, switch to the ALLOWED
    // state.
    factory_reset_state_ = FactoryResetState::ALLOWED;
  }

  if (!allowed) {
    // If the reset button was held, cancel the button countdown and notify the state change.
    if (factory_reset_state_ == FactoryResetState::BUTTON_COUNTDOWN) {
      start_reset_countdown_after_timeout_.Cancel();
    }

    // If the reset button was held, cancel the reset countdown and notify the state change.
    if (factory_reset_state_ == FactoryResetState::RESET_COUNTDOWN) {
      reset_after_timeout_.Cancel();
      deadline_ = ZX_TIME_INFINITE_PAST;
      NotifyStateChange();
    }

    // Disable Factory reset.
    factory_reset_state_ = FactoryResetState::DISALLOWED;
  }
}

}  // namespace root_presenter
