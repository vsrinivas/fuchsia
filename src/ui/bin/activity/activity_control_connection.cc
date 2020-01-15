// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/activity/activity_control_connection.h"

#include <fuchsia/ui/activity/control/cpp/fidl.h>
#include <lib/async/cpp/time.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <src/lib/fxl/logging.h>

namespace activity {

ActivityControlConnection::ActivityControlConnection(
    StateMachineDriver* state_machine_driver, async_dispatcher_t* dispatcher,
    fidl::InterfaceRequest<fuchsia::ui::activity::control::Control> request)
    : state_machine_driver_(state_machine_driver), binding_(this, std::move(request), dispatcher) {}

void ActivityControlConnection::SetState(fuchsia::ui::activity::State state) {
  state_machine_driver_->SetOverrideState(state);
}

}  // namespace activity
