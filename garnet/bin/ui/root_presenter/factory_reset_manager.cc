// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/factory_reset_manager.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/component/cpp/startup_context.h>
#include <zircon/status.h>

namespace root_presenter {

FactoryResetManager::FactoryResetManager(component::StartupContext* context) {
  FXL_DCHECK(context);
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

void FactoryResetManager::TriggerFactoryReset() {
  FXL_LOG(WARNING) << "Triggering factory reset";
  countdown_started_ = false;

  FXL_DCHECK(factory_reset_);
  factory_reset_->Reset([](zx_status_t status) {
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Factory service failed with status: "
                     << zx_status_get_string(status);
    }
  });
}

void FactoryResetManager::StartFactoryResetCountdown() {
  if (countdown_started_) {
    return;
  }

  FXL_LOG(WARNING) << "Starting factory reset countdown";
  countdown_started_ = true;

  reset_after_timeout_.Reset(
      fit::bind_member(this, &FactoryResetManager::TriggerFactoryReset));
  async::PostDelayedTask(async_get_default_dispatcher(),
                         reset_after_timeout_.callback(), kCountdownDuration);
}

void FactoryResetManager::CancelFactoryResetCountdown() {
  FXL_LOG(WARNING) << "Factory reset canceled";
  reset_after_timeout_.Cancel();
  countdown_started_ = false;
}

}  // namespace root_presenter
