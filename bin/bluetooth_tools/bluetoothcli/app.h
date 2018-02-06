// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <unordered_map>

#include <fuchsia/cpp/bluetooth_control.h>

#include "garnet/bin/bluetooth_tools/lib/command_dispatcher.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fxl/macros.h"

namespace bluetoothcli {

class App final : public bluetooth_control::ControlDelegate,
                  public bluetooth_control::RemoteDeviceDelegate {
 public:
  App();

  void ReadNextInput();

  bluetooth_control::Control* control() const { return control_.get(); }
  const bluetooth_control::AdapterInfo* active_adapter() const {
    return active_adapter_.get();
  }

  const bluetooth_tools::CommandDispatcher& command_dispatcher() const {
    return command_dispatcher_;
  }

  using DeviceMap =
      std::unordered_map<std::string, bluetooth_control::RemoteDevice>;
  const DeviceMap& discovered_devices() const { return discovered_devices_; }

 private:
  // bluetooth_control::ControlDelegate overrides:
  // TODO(armansito): since this tool is single-threaded the delegate callbacks
  // won't run immediately if ReadNextInput() is blocking to read from stdin. It
  // would be nice to make these more responsive by making this multi-threaded
  // but it's not urgent.
  void OnActiveAdapterChanged(
      bluetooth_control::AdapterInfoPtr adapter) override;
  void OnAdapterUpdated(bluetooth_control::AdapterInfo adapter) override;
  void OnAdapterRemoved(::fidl::StringPtr identifier) override;

  // bluetooth_control::RemoteDeviceDelegate overrides:
  void OnDeviceUpdated(bluetooth_control::RemoteDevice device) override;
  void OnDeviceRemoved(::fidl::StringPtr identifier) override;

  bluetooth_tools::CommandDispatcher command_dispatcher_;

  std::unique_ptr<component::ApplicationContext> context_;
  bluetooth_control::ControlPtr control_;
  bluetooth_control::AdapterInfoPtr active_adapter_;

  // Local ControlDelegate binding.
  fidl::Binding<bluetooth_control::ControlDelegate> control_delegate_;

  // Local RemoteDeviceDelegate bindings.
  fidl::Binding<bluetooth_control::RemoteDeviceDelegate>
      remote_device_delegate_;

  DeviceMap discovered_devices_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace bluetoothcli
