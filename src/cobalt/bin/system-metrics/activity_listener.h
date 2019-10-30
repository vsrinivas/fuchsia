// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_SYSTEM_METRICS_ACTIVITY_LISTENER_H_
#define SRC_COBALT_BIN_SYSTEM_METRICS_ACTIVITY_LISTENER_H_

#include <fuchsia/ui/activity/cpp/fidl.h>

#include "sdk/lib/fidl/cpp/binding.h"

namespace cobalt {

class ActivityListener : public fuchsia::ui::activity::Listener {
 public:
  ActivityListener(fit::function<void(fuchsia::ui::activity::State)> callback)
      : state_update_callback_(std::move(callback)) {}

  // Creates a new handle which can be passed to fuchsia.ui.activity.Provider.WatchState
  // to register this listener to the provider.
  fidl::InterfaceHandle<fuchsia::ui::activity::Listener> NewHandle(
      async_dispatcher_t* dispatcher = nullptr);

  void OnStateChanged(fuchsia::ui::activity::State state, zx_time_t transition_time,
                      OnStateChangedCallback callback) override;

 private:
  fidl::Binding<fuchsia::ui::activity::Listener> binding_{this};
  fit::function<void(fuchsia::ui::activity::State)> state_update_callback_;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_SYSTEM_METRICS_ACTIVITY_LISTENER_H_
