// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_ACTIVITY_ACTIVITY_PROVIDER_CONNECTION_H_
#define SRC_SYS_ACTIVITY_ACTIVITY_PROVIDER_CONNECTION_H_

#include <fuchsia/ui/activity/cpp/fidl.h>
#include <inttypes.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <optional>
#include <queue>
#include <random>

#include "src/lib/fxl/macros.h"
#include "src/sys/activity/common.h"
#include "src/sys/activity/state_machine_driver.h"

namespace activity {

// ActivityProviderConnection is the server-side implementation of the activity
// service's fuchsia.ui.activity.Provider FIDL interface.
//
// One instance of ActivityProviderConnection is created to manage the
// connection with a single client.
class ActivityProviderConnection : public fuchsia::ui::activity::Provider {
 public:
  ActivityProviderConnection(StateMachineDriver* state_machine_driver,
                             async_dispatcher_t* dispatcher,
                             fidl::InterfaceRequest<fuchsia::ui::activity::Provider> request,
                             uint32_t random_seed);
  ~ActivityProviderConnection();

  // Cleans up any resources owned by the instance, including unregistering it as an observer
  // with |state_machine_driver_|.
  zx_status_t Stop();

  void set_error_handler(fit::function<void(zx_status_t)> callback) {
    binding_.set_error_handler(std::move(callback));
  }

  // fuchsia::ui::activity::Provider API
  virtual void WatchState(::fidl::InterfaceHandle<fuchsia::ui::activity::Listener> listener);

 private:
  void OnStateChanged(fuchsia::ui::activity::State state, zx::time transition_time);

  // Publish the latest state to |listener_| if there is any new state to send.
  //
  // When new state is published, PublishStateIfAvailable() will be recursively
  // invoked (asynchronously) once the listener finishes receiving the state.
  // Thus, this method will continuously publish state to the client as quickly
  // as the client can receive it, until the client has observed all state
  // (i.e. until |state_changes_| is empty).
  //
  // Once |state_changes_| is empty, this method sets |listener_ready_| and
  // returns. The next call to OnStateChanged() will (asynchronously) invoke
  // this method.
  //
  // Expected to be run within |dispatcher_|.
  void PublishStateIfAvailable();
  void PublishState();

  ObserverId GenerateObserverId();

  struct StateChange {
    StateChange(fuchsia::ui::activity::State state, zx::time time) : state(state), time(time) {}
    fuchsia::ui::activity::State state;
    zx::time time;
  };

  StateMachineDriver* state_machine_driver_;
  std::optional<ObserverId> observer_id_;
  std::default_random_engine random_;

  // FIFO of state changes which have been observed but have not yet been sent
  // to the Listener client.
  std::queue<StateChange> state_changes_;
  bool listener_ready_;

  async_dispatcher_t* dispatcher_;
  async::TaskClosureMethod<ActivityProviderConnection,
                           &ActivityProviderConnection::PublishStateIfAvailable>
      publish_state_task_{this};

  fidl::InterfacePtr<fuchsia::ui::activity::Listener> listener_;
  fidl::Binding<fuchsia::ui::activity::Provider> binding_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(ActivityProviderConnection);
};

}  // namespace activity

#endif  // SRC_SYS_ACTIVITY_ACTIVITY_PROVIDER_CONNECTION_H_
