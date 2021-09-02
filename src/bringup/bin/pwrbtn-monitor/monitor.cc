// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/pwrbtn-monitor/monitor.h"

#include <fidl/fuchsia.hardware.power.statecontrol/cpp/wire.h>
#include <lib/service/llcpp/service.h>
#include <zircon/status.h>

namespace pwrbtn {

namespace statecontrol_fidl = fuchsia_hardware_power_statecontrol;

void PowerButtonMonitor::GetAction(GetActionRequestView view, GetActionCompleter::Sync& completer) {
  completer.Reply(action_);
}

void PowerButtonMonitor::SetAction(SetActionRequestView view, SetActionCompleter::Sync& completer) {
  action_ = view->action;
  completer.Reply();
}

zx_status_t PowerButtonMonitor::DoAction() {
  switch (action_) {
    case Action::kIgnore:
      return ZX_OK;
    case Action::kShutdown:
      return SendPoweroff();
    default:
      printf("pwrbtn-monitor: unknown action %d\n", int(action_));
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t PowerButtonMonitor::SendPoweroff() {
  auto connect_result = service::Connect<statecontrol_fidl::Admin>();
  if (connect_result.is_error()) {
    printf("pwrbtn-monitor: Failed to connect to statecontrol service: %s\n",
           zx_status_get_string(connect_result.status_value()));
    return connect_result.status_value();
  }
  auto admin_client = fidl::BindSyncClient(std::move(connect_result.value()));
  auto resp = admin_client.Poweroff();
  if (!resp.ok()) {
    printf("pwrbtn-monitor: Call to statecontrol failed: %s\n", resp.FormatDescription().c_str());
    return resp.status();
  }

  return ZX_OK;
}

}  // namespace pwrbtn
