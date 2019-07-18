// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/factory_reset_manager.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/async/time.h>
#include <lib/component/cpp/startup_context.h>
#include <zircon/assert.h>
#include <zircon/status.h>

namespace root_presenter {

namespace {

bool AreStatesEqual(fuchsia::recovery::FactoryResetState& state,
                    fuchsia::recovery::FactoryResetState& state2) {
  if (state.has_reset_deadline() != state2.has_reset_deadline() ||
      state.has_counting_down() != state2.has_counting_down()) {
    return false;
  }

  if (state.has_reset_deadline() && state.reset_deadline() != state2.reset_deadline()) {
    return false;
  }

  if (state.has_counting_down() && state.counting_down() != state2.counting_down()) {
    return false;
  }

  return true;
}

}  // namespace

FactoryResetManager::Notifier::Notifier(fuchsia::recovery::FactoryResetStateWatcherPtr watcher)
    : watcher_(std::move(watcher)) {
  ZX_ASSERT(watcher_);
}

void FactoryResetManager::Notifier::Notify(fuchsia::recovery::FactoryResetState state) {
  pending_ = std::move(state);
  SendIfPending();
}

void FactoryResetManager::Notifier::SendIfPending() {
  if (notification_in_progress_ || AreStatesEqual(last_, pending_)) {
    return;
  }

  Send();
}

void FactoryResetManager::Notifier::Send() {
  notification_in_progress_ = true;
  last_ = std::move(pending_);

  watcher_->OnStateChanged(fidl::Clone(last_), [this]() {
    notification_in_progress_ = false;
    SendIfPending();
  });
}

FactoryResetManager::FactoryResetManager(component::StartupContext* context) {
  FXL_DCHECK(context);
  context->outgoing().AddPublicService(notifier_bindings_.GetHandler(this));
  context->ConnectToEnvironmentService<fuchsia::recovery::FactoryReset>(
      factory_reset_.NewRequest());
  FXL_DCHECK(factory_reset_);
}

bool FactoryResetManager::OnMediaButtonReport(
    const fuchsia::ui::input::MediaButtonsReport& report) {
  if (report.reset) {
    StartFactoryResetCountdown();
    return true;
  } else {
    if (countdown_started_) {
      CancelFactoryResetCountdown();
      return true;
    }
  }

  return false;
}

void FactoryResetManager::SetWatcher(
    fidl::InterfaceHandle<fuchsia::recovery::FactoryResetStateWatcher> watcher) {
  notifier_ = std::make_unique<Notifier>(watcher.Bind());
  NotifyStateChange();
}

void FactoryResetManager::TriggerFactoryReset() {
  FXL_LOG(WARNING) << "Triggering factory reset";
  countdown_started_ = false;

  FXL_DCHECK(factory_reset_);
  factory_reset_->Reset([](zx_status_t status) {
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Factory service failed with status: " << zx_status_get_string(status);
    }
  });
}

void FactoryResetManager::NotifyStateChange() {
  if (notifier_) {
    fuchsia::recovery::FactoryResetState state;
    state.set_counting_down(countdown_started_);

    if (countdown_started_) {
      state.set_reset_deadline(deadline_);
    }
    notifier_->Notify(std::move(state));
  }
}

void FactoryResetManager::StartFactoryResetCountdown() {
  if (countdown_started_) {
    return;
  }

  FXL_LOG(WARNING) << "Starting factory reset countdown";
  countdown_started_ = true;
  deadline_ = async_now(async_get_default_dispatcher()) + kCountdownDuration.get();
  NotifyStateChange();

  reset_after_timeout_.Reset(fit::bind_member(this, &FactoryResetManager::TriggerFactoryReset));
  async::PostDelayedTask(async_get_default_dispatcher(), reset_after_timeout_.callback(),
                         kCountdownDuration);
}

void FactoryResetManager::CancelFactoryResetCountdown() {
  FXL_LOG(WARNING) << "Factory reset canceled";
  reset_after_timeout_.Cancel();
  countdown_started_ = false;
  deadline_ = ZX_TIME_INFINITE_PAST;
  NotifyStateChange();
}

}  // namespace root_presenter
