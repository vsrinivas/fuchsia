// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_APP_ACTIVITY_LISTENER_IMPL_H_
#define SRC_COBALT_BIN_APP_ACTIVITY_LISTENER_IMPL_H_

#include <fuchsia/ui/activity/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/sys/inspect/cpp/component.h>

#include <functional>
#include <optional>

#include "src/lib/backoff/exponential_backoff.h"
#include "src/lib/fxl/functional/cancelable_callback.h"
#include "src/lib/fxl/macros.h"
#include "third_party/cobalt/src/public/activity_listener_interface.h"

namespace cobalt {

// After a callback is registered with Start(std::function<void(ActivityState> callback), this class
// invokes the callback with state information once connected to the service (but not before)
// and each time the ActivityState changes.
//
// In case of failure, e.g., loss of connection, error returned, the activity state is set
// to UNKNOWN regardless of its current state and the connection to the service will be
// severed. Following an exponential backoff, the connection will be re-established.
//
// Wraps around fuchsia::ui::activity to handle establishing the connection, losing the
// connection, and receiving state updates through cobalt::ui::activity::Listener's OnStateChanged()
class ActivityListenerImpl : public cobalt::ActivityListenerInterface,
                             public fuchsia::ui::activity::Listener {
 public:
  ActivityListenerImpl(async_dispatcher_t* dispatcher,
                       std::shared_ptr<sys::ServiceDirectory> services);
  ~ActivityListenerImpl() = default;

  void OnStateChanged(fuchsia::ui::activity::State state, zx_time_t transition_time,
                      OnStateChangedCallback callback) override;

  void Start(const std::function<void(ActivityState)>& callback) override;

  ActivityState state() override { return state_; }

  bool IsConnected() { return activity_state_ptr_.is_bound(); }

 private:
  void RestartListening();
  void SetState(fuchsia::ui::activity::State state);
  void Update();
  void StartListening();

  ActivityState state_ = ActivityState::UNKNOWN;
  std::optional<std::function<void(ActivityState)>> callback_;
  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  fidl::Binding<fuchsia::ui::activity::Listener> binding_{this};
  fuchsia::ui::activity::ProviderPtr activity_state_ptr_;
  backoff::ExponentialBackoff backoff_;
  fxl::CancelableClosure reconnect_task_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ActivityListenerImpl);
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_APP_ACTIVITY_LISTENER_IMPL_H_
