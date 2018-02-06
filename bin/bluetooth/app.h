// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <vector>

#include <fuchsia/cpp/bluetooth_control.h>
#include <fuchsia/cpp/bluetooth_gatt.h>
#include <fuchsia/cpp/bluetooth_low_energy.h>

#include "garnet/bin/bluetooth/bluetooth_manager.h"
#include "garnet/bin/bluetooth/control_server.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace bluetooth_service {

// The App class represents the Bluetooth system service application. It owns
// the BluetoothManager and resolves FIDL service requests.
//
// When a FIDL service request is received for an interface that is tied to a
// bt-host device, the provided channel handle is forwarded to the current
// active adapter device. The interface handle is owned by the bt-host device as
// long as the device exists and remains the active adapter.
class App final {
 public:
  explicit App(
      std::unique_ptr<component::ApplicationContext> application_context);
  ~App() = default;

  // Returns the underlying BluetoothManager that owns the gap::Adapter
  // instances.
  BluetoothManager* manager() { return &manager_; }

 private:
  // BluetoothManager callbacks:
  void OnActiveAdapterChanged(
      const bluetooth_control::AdapterInfoPtr& info_ptr);
  void OnAdapterUpdated(const bluetooth_control::AdapterInfoPtr& info_ptr);
  void OnAdapterRemoved(const std::string& identifier);
  void OnDeviceUpdated(const bluetooth_control::RemoteDevice& device);

  // Called when there is an interface request for the BluetoothManager FIDL
  // service.
  void OnControlRequest(
      fidl::InterfaceRequest<::bluetooth_control::Control> request);

  // Called when there is an interface request for the low_energy::Central FIDL
  // service.
  void OnLowEnergyCentralRequest(
      fidl::InterfaceRequest<::bluetooth_low_energy::Central> request);

  // Called when there is an interface request for the low_energy::Peripheral
  // FIDL service.
  void OnLowEnergyPeripheralRequest(
      fidl::InterfaceRequest<::bluetooth_low_energy::Peripheral> request);

  // Called when there is an interface request for the gatt::Server FIDL
  // service.
  void OnGattServerRequest(
      fidl::InterfaceRequest<::bluetooth_gatt::Server> request);

  // Called when a BluetoothManagerServer that we own notifies a connection
  // error handler.
  void OnControlServerDisconnected(ControlServer* server);

  // Provides access to the environment. This is used to publish outgoing
  // services.
  std::unique_ptr<component::ApplicationContext> application_context_;

  // Watches for Bluetooth HCI devices and notifies us when adapters get added
  // and removed.
  BluetoothManager manager_;

  // The list of BluetoothManager FIDL interface handles that have been vended
  // out.
  std::vector<std::unique_ptr<ControlServer>> servers_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  fxl::WeakPtrFactory<App> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace bluetooth_service
