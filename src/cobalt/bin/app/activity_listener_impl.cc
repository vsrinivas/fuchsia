// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/activity_listener_impl.h"

#include <iostream>

#include "fuchsia/ui/activity/cpp/fidl.h"
#include "lib/async/cpp/task.h"
#include "src/public/activity_listener_interface.h"

namespace cobalt {

std::map<fuchsia::ui::activity::State, ActivityState> state_map = {
    {fuchsia::ui::activity::State::ACTIVE, ActivityState::ACTIVE},
    {fuchsia::ui::activity::State::IDLE, ActivityState::IDLE},
    {fuchsia::ui::activity::State::UNKNOWN, ActivityState::UNKNOWN},
};

ActivityListenerImpl::ActivityListenerImpl(async_dispatcher_t* dispatcher,
                                           std::shared_ptr<sys::ServiceDirectory> services)
    : dispatcher_(dispatcher),
      services_(std::move(services)),
      backoff_(/*initial_delay=*/zx::msec(100), /*retry_factor=*/2u, /*max_delay=*/zx::hour(1)) {}

void ActivityListenerImpl::OnStateChanged(fuchsia::ui::activity::State state,
                                          zx_time_t transition_time,
                                          OnStateChangedCallback callback) {
  SetState(state);
  callback();
}

void ActivityListenerImpl::Start(const std::function<void(cobalt::ActivityState)>& callback) {
  if (callback_) {
    FX_LOGS(ERROR) << "Callback value already set. Replacing the current value.";
  }
  callback_ = callback;
  // TODO(fxbug.dev/113288): this is only temporary until Cobalt Core does not do any activity
  // listening.
  SetState(fuchsia::ui::activity::State::IDLE);
  callback_.value()(ActivityState::IDLE);
  // TODO(fxbug.dev/107587): remove the FIDL dependency instead of simply commenting it out.
  // StartListening();
  // Update();
}

void ActivityListenerImpl::StartListening() {
  activity_state_ptr_ = services_->Connect<fuchsia::ui::activity::Provider>();
  activity_state_ptr_.set_error_handler([this](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.ui.activity";
    RestartListening();
  });

  activity_state_ptr_->WatchState(binding_.NewBinding(dispatcher_));
}

void ActivityListenerImpl::RestartListening() {
  SetState(fuchsia::ui::activity::State::UNKNOWN);
  activity_state_ptr_.Unbind();

  reconnect_task_.Reset([this] { StartListening(); });
  async::PostDelayedTask(dispatcher_, reconnect_task_.callback(), backoff_.GetNext());
}

void ActivityListenerImpl::SetState(fuchsia::ui::activity::State state) {
  state_ = state_map[state];
  Update();
}

void ActivityListenerImpl::Update() {
  if (callback_) {
    callback_.value()(state_);
  }
}

}  // namespace cobalt
