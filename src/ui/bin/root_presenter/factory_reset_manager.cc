// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/factory_reset_manager.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/async/time.h>
#include <zircon/assert.h>
#include <zircon/status.h>

namespace root_presenter {

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

FactoryResetManager::FactoryResetManager(sys::ComponentContext& context) {
  context.outgoing()->AddPublicService<fuchsia::recovery::ui::FactoryResetCountdown>(
      [this](fidl::InterfaceRequest<fuchsia::recovery::ui::FactoryResetCountdown> request) {
        auto handler = std::make_unique<WatchHandler>(State());
        countdown_bindings_.AddBinding(std::move(handler), std::move(request));
      });

  context.svc()->Connect(factory_reset_.NewRequest());
  FX_DCHECK(factory_reset_);
}

bool FactoryResetManager::OnMediaButtonReport(
    const fuchsia::ui::input::MediaButtonsReport& report) {
  if (report.reset) {
    StartFactoryResetCountdown();
    return true;
  }

  if (countdown_started_) {
    CancelFactoryResetCountdown();
    return true;
  }

  return false;
}

void FactoryResetManager::TriggerFactoryReset() {
  FX_LOGS(WARNING) << "Triggering factory reset";
  countdown_started_ = false;

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
  if (countdown_started_) {
    state.set_scheduled_reset_time(deadline_);
  }
  return state;
}

void FactoryResetManager::StartFactoryResetCountdown() {
  if (countdown_started_) {
    return;
  }

  FX_LOGS(WARNING) << "Starting factory reset countdown";
  countdown_started_ = true;
  deadline_ = async_now(async_get_default_dispatcher()) + kCountdownDuration.get();
  NotifyStateChange();

  reset_after_timeout_.Reset(fit::bind_member(this, &FactoryResetManager::TriggerFactoryReset));
  async::PostDelayedTask(async_get_default_dispatcher(), reset_after_timeout_.callback(),
                         kCountdownDuration);
}

void FactoryResetManager::CancelFactoryResetCountdown() {
  FX_LOGS(WARNING) << "Factory reset canceled";
  reset_after_timeout_.Cancel();
  countdown_started_ = false;
  deadline_ = ZX_TIME_INFINITE_PAST;
  NotifyStateChange();
}

}  // namespace root_presenter
