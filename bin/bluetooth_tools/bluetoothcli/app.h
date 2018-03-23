// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <unordered_map>

#include "garnet/bin/bluetooth_tools/lib//command_dispatcher.h"
#include "lib/app/cpp/application_context.h"
#include <fuchsia/cpp/bluetooth_control.h>
#include "lib/fxl/macros.h"

namespace bluetoothcli {

class App final : public bluetooth_control::AdapterManagerDelegate,
                  public bluetooth_control::AdapterDelegate {
 public:
  App();

  void ReadNextInput();

  bluetooth_control::AdapterManager* adapter_manager() const {
    return adapter_manager_.get();
  }
  bluetooth_control::Adapter* active_adapter() const {
    return active_adapter_.get();
  }

  const bluetooth_tools::CommandDispatcher& command_dispatcher() const {
    return command_dispatcher_;
  }

  using DeviceMap =
      std::unordered_map<std::string, bluetooth_control::RemoteDevice>;
  const DeviceMap& discovered_devices() const { return discovered_devices_; }

 private:
  // bluetooth_control::AdapterManagerDelegate overrides:
  // TODO(armansito): since this tool is single-threaded the delegate callbacks
  // won't run immediately if ReadNextInput() is blocking to read from stdin. It
  // would be nice to make these more responsive by making this multi-threaded
  // but it's not urgent.
  void OnActiveAdapterChanged(
      bluetooth_control::AdapterInfoPtr active_adapter) override;
  void OnAdapterAdded(bluetooth_control::AdapterInfo adapter) override;
  void OnAdapterRemoved(::fidl::StringPtr identifier) override;

  // bluetooth_control::AdapterDelegate overrides:
  void OnAdapterStateChanged(
      bluetooth_control::AdapterState state) override;
  void OnDeviceDiscovered(bluetooth_control::RemoteDevice device) override;

  bluetooth_tools::CommandDispatcher command_dispatcher_;

  std::unique_ptr<component::ApplicationContext> context_;
  bluetooth_control::AdapterManagerPtr adapter_manager_;
  bluetooth_control::AdapterPtr active_adapter_;

  // Local AdapterManagerDelegate binding.
  fidl::Binding<bluetooth_control::AdapterManagerDelegate> manager_delegate_;

  // Local AdapterDelegate binding.
  fidl::Binding<bluetooth_control::AdapterDelegate> adapter_delegate_;

  DeviceMap discovered_devices_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace bluetoothcli
