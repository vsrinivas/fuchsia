// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/activity/activity_provider_connection.h"

#include <fuchsia/ui/activity/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <zircon/status.h>
#include <zircon/types.h>

namespace activity {

ActivityProviderConnection::ActivityProviderConnection(
    StateMachineDriver* state_machine_driver, async_dispatcher_t* dispatcher,
    fidl::InterfaceRequest<fuchsia::ui::activity::Provider> request, uint32_t random_seed)
    : state_machine_driver_(state_machine_driver),
      random_(random_seed),
      dispatcher_(dispatcher),
      binding_(this, std::move(request), dispatcher) {}

ActivityProviderConnection::~ActivityProviderConnection() { Stop(); }

zx_status_t ActivityProviderConnection::Stop() {
  if (observer_id_) {
    FX_LOGS(INFO) << "activity-service: Listener " << *observer_id_ << " stopping";
    state_machine_driver_->UnregisterObserver(*observer_id_);
  }
  if (publish_state_task_.is_pending()) {
    publish_state_task_.Cancel();
  }
  return ZX_OK;
}

void ActivityProviderConnection::WatchState(
    fidl::InterfaceHandle<fuchsia::ui::activity::Listener> listener) {
  FX_LOGS(INFO) << "activity-service: Registering listener";

  // WatchState should only be called once per connection.
  if (listener_.is_bound()) {
    FX_LOGS(WARNING) << "activity-service: WatchState called twice on same connection; "
                     << "ignoring";
    return;
  }

  auto id = GenerateObserverId();
  StateChangedCallback callback = std::bind(&ActivityProviderConnection::OnStateChanged, this,
                                            std::placeholders::_1, std::placeholders::_2);
  auto status = state_machine_driver_->RegisterObserver(id, std::move(callback));
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "activity-service: failed to register state observer: "
                     << zx_status_get_string(status);
    return;
  }
  observer_id_ = id;
  FX_LOGS(INFO) << "activity-service: Obtained observer ID " << id;

  listener_ = listener.Bind();
  listener_.set_error_handler([this](zx_status_t status) { Stop(); });

  // Publish the current state immediately
  state_changes_.emplace(state_machine_driver_->GetState(), zx::clock::get_monotonic());
  PublishState();
}

void ActivityProviderConnection::OnStateChanged(fuchsia::ui::activity::State state,
                                                zx::time transition_time) {
  if (!state_changes_.empty() && state_changes_.front().time > transition_time) {
    // Only enqueue state changes in monotonically increasing time
    return;
  }
  state_changes_.emplace(state, transition_time);
  if (listener_ready_ && !publish_state_task_.is_pending()) {
    auto status = publish_state_task_.Post(dispatcher_);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "activity-service: Failed to post state change task: "
                     << zx_status_get_string(status);
    }
  }
}

void ActivityProviderConnection::PublishStateIfAvailable() {
  if (!state_changes_.empty()) {
    PublishState();
  } else {
    listener_ready_ = true;
  }
}

void ActivityProviderConnection::PublishState() {
  ZX_DEBUG_ASSERT(!state_changes_.empty());
  listener_ready_ = false;
  auto state_change = state_changes_.front();
  state_changes_.pop();
  listener_->OnStateChanged(state_change.state, state_change.time.get(),
                            [this]() { PublishStateIfAvailable(); });
}

ObserverId ActivityProviderConnection::GenerateObserverId() {
  return static_cast<ObserverId>(random_());
}

}  // namespace activity
