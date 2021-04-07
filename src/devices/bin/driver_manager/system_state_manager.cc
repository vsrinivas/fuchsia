// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system_state_manager.h"

#include <lib/fidl-async/cpp/bind.h>
#include <zircon/status.h>

#include "src/devices/lib/log/log.h"

zx_status_t SystemStateManager::Create(async_dispatcher_t* dispatcher, Coordinator* dev_coord,
                                       zx::channel system_state_transition_server,
                                       std::unique_ptr<SystemStateManager>* state_mgr) {
  // Invoked when the channel is closed or on any binding-related error.
  // When power manager exists, but closes this channel, it means power manager
  // existed but crashed, and we will not have a way to reboot the system.
  // We would need to reboot in that case.
  fidl::OnUnboundFn<SystemStateManager> unbound_fn(
      [](SystemStateManager* sys_state_manager, fidl::UnbindInfo info,
         fidl::ServerEnd<fuchsia_device_manager::SystemStateTransition>) {
        LOGF(ERROR, "system state transition channel with power manager got unbound:%d",
             static_cast<uint32_t>(info.reason));
        SystemStateManager* system_state_manager =
            static_cast<SystemStateManager*>(sys_state_manager);
        Coordinator* dev_coord = system_state_manager->dev_coord_;
        if (!dev_coord->power_manager_registered()) {
          return;
        }
        dev_coord->set_power_manager_registered(false);
      });
  auto mgr = std::make_unique<SystemStateManager>(dev_coord);
  auto result = fidl::BindServer(dispatcher, std::move(system_state_transition_server), mgr.get(),
                                 std::move(unbound_fn));
  if (!result.is_ok()) {
    LOGF(ERROR, "Failed to bind to client channel for '%s': %s",
         device_manager_fidl::SystemStateTransition::Name, zx_status_get_string(result.error()));
    return result.error();
  }
  *state_mgr = std::move(mgr);
  return ZX_OK;
}

void SystemStateManager::SetTerminationSystemState(
    statecontrol_fidl::wire::SystemPowerState state,
    SetTerminationSystemStateCompleter::Sync& completer) {
  if (state == statecontrol_fidl::wire::SystemPowerState::FULLY_ON) {
    LOGF(INFO, "Invalid termination state");
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }
  LOGF(INFO, "Setting shutdown system state to %d", state);
  dev_coord_->set_shutdown_system_state(state);
  completer.ReplySuccess();
}
