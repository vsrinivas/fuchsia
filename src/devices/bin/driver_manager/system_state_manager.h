// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_SYSTEM_STATE_MANAGER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_SYSTEM_STATE_MANAGER_H_

#include <fuchsia/device/manager/llcpp/fidl.h>
#include <lib/zx/channel.h>

#include "coordinator.h"

namespace device_manager_fidl = llcpp::fuchsia::device::manager;
namespace power_fidl = llcpp::fuchsia::hardware::power;

class SystemStateManager : public device_manager_fidl::SystemStateTransition::Interface {
 public:
  explicit SystemStateManager(Coordinator* dev_coord) : dev_coord_(dev_coord) {}

  static zx_status_t Create(async_dispatcher_t* dispatcher, Coordinator* dev_coord,
                            zx::channel system_state_transition_server,
                            std::unique_ptr<SystemStateManager>* out);

  // SystemStateTransition interface
  void SetTerminationSystemState(power_fidl::statecontrol::SystemPowerState state,
                                 device_manager_fidl::SystemStateTransition::Interface::
                                     SetTerminationSystemStateCompleter::Sync completer) override;

 private:
  Coordinator* dev_coord_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_SYSTEM_STATE_MANAGER_H_
