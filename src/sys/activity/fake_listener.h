// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_ACTIVITY_FAKE_LISTENER_H_
#define SRC_SYS_ACTIVITY_FAKE_LISTENER_H_

#include <fuchsia/ui/activity/cpp/fidl.h>

#include "lib/async/dispatcher.h"

namespace activity {
namespace testing {

// Test fixture for faking a listener to the fuchsia.ui.activity.Provider API.
class FakeListener : public fuchsia::ui::activity::Listener {
 public:
  FakeListener() = default;

  // Creates a new handle which can be passed to fuchsia.ui.activity.Provider.WatchState
  // to register this listener to the provider.
  fidl::InterfaceHandle<fuchsia::ui::activity::Listener> NewHandle(
      async_dispatcher_t* dispatcher = nullptr) {
    return binding_.NewBinding(dispatcher);
  }

  // fuchsia.ui.activity.Listener API
  virtual void OnStateChanged(fuchsia::ui::activity::State state, zx_time_t transition_time,
                              OnStateChangedCallback callback) {
    state_changes_.emplace_back(state, transition_time);
    callback();
  }

  struct StateChange {
    StateChange(fuchsia::ui::activity::State state, zx_time_t time) : state(state), time(time) {}
    fuchsia::ui::activity::State state;
    zx::time time;
  };

  // Returns a list of state changes received by the listener.
  const std::vector<StateChange>& StateChanges() const { return state_changes_; }

 private:
  fidl::Binding<fuchsia::ui::activity::Listener> binding_{this};
  std::vector<StateChange> state_changes_;
};

}  // namespace testing
}  // namespace activity

#endif  // SRC_SYS_ACTIVITY_FAKE_LISTENER_H_
