// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <unordered_map>

#include "lib/app/cpp/application_context.h"
#include "lib/bluetooth/fidl/control.fidl.h"
#include "garnet/bin/bluetooth_tools/lib//command_dispatcher.h"
#include "lib/fxl/macros.h"

namespace bluetoothcli {

class App final : public bluetooth::control::AdapterManagerDelegate,
                  public bluetooth::control::AdapterDelegate {
 public:
  App();

  void ReadNextInput();

  bluetooth::control::AdapterManager* adapter_manager() const { return adapter_manager_.get(); }
  bluetooth::control::Adapter* active_adapter() const { return active_adapter_.get(); }

  const bluetooth::tools::CommandDispatcher& command_dispatcher() const {
    return command_dispatcher_;
  }

  using DeviceMap = std::unordered_map<std::string, bluetooth::control::RemoteDevicePtr>;
  const DeviceMap& discovered_devices() const { return discovered_devices_; }

 private:
  // bluetooth::control::AdapterManagerDelegate overrides:
  // TODO(armansito): since this tool is single-threaded the delegate callbacks won't run
  // immediately if ReadNextInput() is blocking to read from stdin. It would be nice to make these
  // more responsive by making this multi-threaded but it's not urgent.
  void OnActiveAdapterChanged(bluetooth::control::AdapterInfoPtr active_adapter) override;
  void OnAdapterAdded(bluetooth::control::AdapterInfoPtr adapter) override;
  void OnAdapterRemoved(const ::fidl::String& identifier) override;

  // bluetooth::control::AdapterDelegate overrides:
  void OnAdapterStateChanged(bluetooth::control::AdapterStatePtr state) override;
  void OnDeviceDiscovered(bluetooth::control::RemoteDevicePtr device) override;

  bluetooth::tools::CommandDispatcher command_dispatcher_;

  std::unique_ptr<app::ApplicationContext> context_;
  bluetooth::control::AdapterManagerPtr adapter_manager_;
  bluetooth::control::AdapterPtr active_adapter_;

  // Local AdapterManagerDelegate binding.
  fidl::Binding<bluetooth::control::AdapterManagerDelegate> manager_delegate_;

  // Local AdapterDelegate binding.
  fidl::Binding<bluetooth::control::AdapterDelegate> adapter_delegate_;

  DeviceMap discovered_devices_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace bluetoothcli
