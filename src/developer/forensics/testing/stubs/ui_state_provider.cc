// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/testing/stubs/ui_state_provider.h"

#include <lib/fit/defer.h>
#include <zircon/types.h>

namespace forensics::stubs {

UIStateProvider::UIStateProvider(async_dispatcher_t* dispatcher, fuchsia::ui::activity::State state,
                                 zx::time time)
    : dispatcher_(dispatcher), state_(state), time_(time) {}

void UIStateProvider::WatchState(
    ::fidl::InterfaceHandle<::fuchsia::ui::activity::Listener> listener) {
  listener_.Bind(std::move(listener), dispatcher_);
  listener_.set_error_handler([](const zx_status_t status) {
    FX_PLOGS(WARNING, status) << "Lost connection to fuchsia.ui.activity listener";
  });

  OnStateChanged();
}

void UIStateProvider::SetState(fuchsia::ui::activity::State state, zx::time time) {
  state_ = state;
  time_ = time;

  if (!binding() || !binding()->is_bound() || !listener_.is_bound()) {
    return;
  }

  OnStateChanged();
}

void UIStateProvider::UnbindListener() { listener_.Unbind(); }

void UIStateProvider::OnStateChanged() {
  auto check_callback = fit::defer(
      [] { FX_LOGS(FATAL) << "fuchsia.ui.activity/Listener.OnStateChange not responded to"; });

  listener_->OnStateChanged(
      state_, time_.get(),
      [check_callback = std::move(check_callback)]() mutable { check_callback.cancel(); });
}

}  // namespace forensics::stubs
