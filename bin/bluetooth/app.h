// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <vector>

#include "garnet/bin/bluetooth/adapter_manager.h"
#include "garnet/bin/bluetooth/adapter_manager_server.h"
#include "lib/app/cpp/application_context.h"
#include "lib/bluetooth/fidl/control.fidl.h"
#include "lib/bluetooth/fidl/gatt.fidl.h"
#include "lib/bluetooth/fidl/low_energy.fidl.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace bluetooth_service {

// The App class represents the Bluetooth system service application. It owns
// the AdapterManager and resolves FIDL service requests.
//
// When a FIDL service request is received for an interface that is tied to a
// bt-host device, the provided channel handle is forwarded to the current
// active adapter device. The interface handle is owned by the bt-host device as
// long as the device exists and remains the active adapter.
class App final {
 public:
  explicit App(std::unique_ptr<app::ApplicationContext> application_context);
  ~App() = default;

  // Returns the underlying AdapterManager that owns the gap::Adapter instances.
  AdapterManager* adapter_manager() { return &adapter_manager_; }

 private:
  // AdapterManager callbacks:
  void OnActiveAdapterChanged(const Adapter* adapter);
  void OnAdapterAdded(const Adapter& adapter);
  void OnAdapterRemoved(const Adapter& adapter);

  // Called when there is an interface request for the AdapterManager FIDL
  // service.
  void OnAdapterManagerRequest(
      fidl::InterfaceRequest<::bluetooth::control::AdapterManager> request);

  // Called when there is an interface request for the low_energy::Central FIDL
  // service.
  void OnLowEnergyCentralRequest(
      fidl::InterfaceRequest<::bluetooth::low_energy::Central> request);

  // Called when there is an interface request for the low_energy::Peripheral
  // FIDL service.
  void OnLowEnergyPeripheralRequest(
      fidl::InterfaceRequest<::bluetooth::low_energy::Peripheral> request);

  // Called when there is an interface request for the gatt::Server FIDL
  // service.
  void OnGattServerRequest(
      fidl::InterfaceRequest<::bluetooth::gatt::Server> request);

  // Called when a AdapterManagerServer that we own notifies a connection
  // error handler.
  void OnAdapterManagerServerDisconnected(AdapterManagerServer* server);

  // Provides access to the environment. This is used to publish outgoing
  // services.
  std::unique_ptr<app::ApplicationContext> application_context_;

  // Watches for Bluetooth HCI devices and notifies us when adapters get added
  // and removed.
  AdapterManager adapter_manager_;

  // The list of AdapterManager FIDL interface handles that have been vended
  // out.
  std::vector<std::unique_ptr<AdapterManagerServer>> servers_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  fxl::WeakPtrFactory<App> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace bluetooth_service
