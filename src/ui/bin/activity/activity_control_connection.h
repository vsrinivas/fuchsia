// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ACTIVITY_ACTIVITY_CONTROL_CONNECTION_H_
#define SRC_UI_BIN_ACTIVITY_ACTIVITY_CONTROL_CONNECTION_H_

#include <fuchsia/ui/activity/control/cpp/fidl.h>
#include <inttypes.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <random>
#include <set>

#include <src/lib/fxl/macros.h>

#include "src/ui/bin/activity/common.h"
#include "src/ui/bin/activity/state_machine_driver.h"

namespace activity {

// ActivityControlConnection is the server-side implementation of the
// activity service's Control FIDL interface.
//
// One instance of ActivityControlConnection is created to manage the
// connection with a single client.
class ActivityControlConnection : public fuchsia::ui::activity::control::Control {
 public:
  ActivityControlConnection(
      StateMachineDriver* state_machine_driver, async_dispatcher_t* dispatcher,
      fidl::InterfaceRequest<fuchsia::ui::activity::control::Control> request);

  void set_error_handler(fit::function<void(zx_status_t)> callback) {
    binding_.set_error_handler(std::move(callback));
  }

  // fuchsia::ui::activity::control::Control API
  virtual void SetState(fuchsia::ui::activity::State state);

 private:
  StateMachineDriver* const state_machine_driver_;

  fidl::Binding<fuchsia::ui::activity::control::Control> binding_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(ActivityControlConnection);
};

}  // namespace activity

#endif  // SRC_UI_BIN_ACTIVITY_ACTIVITY_CONTROL_CONNECTION_H_
