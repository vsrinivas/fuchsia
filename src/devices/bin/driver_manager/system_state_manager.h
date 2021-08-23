// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_SYSTEM_STATE_MANAGER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_SYSTEM_STATE_MANAGER_H_

#include <fuchsia/device/manager/llcpp/fidl.h>
#include <lib/zx/channel.h>

class Coordinator;

namespace device_manager_fidl = fuchsia_device_manager;
namespace statecontrol_fidl = fuchsia_hardware_power_statecontrol;

class SystemStateManager : public fidl::WireServer<device_manager_fidl::SystemStateTransition> {
 public:
  explicit SystemStateManager(Coordinator* dev_coord) : dev_coord_(dev_coord) {}

  static zx_status_t Create(async_dispatcher_t* dispatcher, Coordinator* dev_coord,
                            fidl::ServerEnd<fuchsia_device_manager::SystemStateTransition> server,
                            std::unique_ptr<SystemStateManager>* out);

  // SystemStateTransition interface
  void SetTerminationSystemState(SetTerminationSystemStateRequestView request,
                                 fidl::WireServer<device_manager_fidl::SystemStateTransition>::
                                     SetTerminationSystemStateCompleter::Sync& completer) override;

 private:
  Coordinator* dev_coord_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_SYSTEM_STATE_MANAGER_H_
