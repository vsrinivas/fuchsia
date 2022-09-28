// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_ACTIVITY_ACTIVITY_APP_H_
#define SRC_SYS_ACTIVITY_ACTIVITY_APP_H_

#include <fuchsia/ui/activity/control/cpp/fidl.h>
#include <fuchsia/ui/activity/cpp/fidl.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>

#include <map>
#include <memory>

#include "src/lib/fxl/macros.h"
#include "src/sys/activity/activity_control_connection.h"
#include "src/sys/activity/activity_provider_connection.h"

namespace activity {

class ActivityApp {
 public:
  ActivityApp(std::unique_ptr<StateMachineDriver> state_machine_driver,
              async_dispatcher_t* dispatcher)
      : state_machine_driver_(std::move(state_machine_driver)), dispatcher_(dispatcher) {}

  // Registers a new Control client and stores a binding created from |request|.
  // The binding is automatically cleaned up when the client terminates, or when a channel
  // error occurs.
  void AddControlBinding(fidl::InterfaceRequest<fuchsia::ui::activity::control::Control> request);
  // Registers a new Provider client and stores a binding created from |request|.
  // The binding is automatically cleaned up when the client terminates, or when a channel
  // error occurs.
  void AddProviderBinding(fidl::InterfaceRequest<fuchsia::ui::activity::Provider> request);

  // Returns a list of weak references to the bindings managed by this instance.
  // Exposed for testing.
  std::vector<const ActivityControlConnection*> control_bindings() const;
  std::vector<const ActivityProviderConnection*> provider_bindings() const;

 private:
  std::unique_ptr<StateMachineDriver> state_machine_driver_;
  async_dispatcher_t* dispatcher_;

  std::map<zx::unowned_channel, std::unique_ptr<ActivityControlConnection>> control_bindings_;
  std::map<zx::unowned_channel, std::unique_ptr<ActivityProviderConnection>> provider_bindings_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(ActivityApp);
};

}  // namespace activity

#endif  // SRC_SYS_ACTIVITY_ACTIVITY_APP_H_
