// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <vector>

#include "lib/app/cpp/application_context.h"
#include "apps/bluetooth/service/interfaces/control.fidl.h"
#include "apps/bluetooth/service/src/adapter_manager.h"
#include "apps/bluetooth/service/src/adapter_manager_fidl_impl.h"
#include "apps/bluetooth/service/src/low_energy_central_fidl_impl.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace bluetooth_service {

// The App class represents the Bluetooth system service application. This class acts as the entry
// point to the Bluetooth system.
class App final : public AdapterManager::Observer {
 public:
  explicit App(std::unique_ptr<app::ApplicationContext> application_context);
  ~App() override;

  // Returns the underlying AdapterManager that owns the gap::Adapter instances.
  AdapterManager* adapter_manager() { return &adapter_manager_; }

 private:
  // AdapterManager::Delegate overrides:
  void OnActiveAdapterChanged(bluetooth::gap::Adapter* adapter) override;
  void OnAdapterCreated(bluetooth::gap::Adapter* adapter) override;
  void OnAdapterRemoved(bluetooth::gap::Adapter* adapter) override;

  // Called when there is an interface request for the AdapterManager FIDL service.
  void OnAdapterManagerRequest(
      ::fidl::InterfaceRequest<::bluetooth::control::AdapterManager> request);

  // Called when there is an interface request for the low_energy::Central FIDL service.
  void OnLowEnergyCentralRequest(
      ::fidl::InterfaceRequest<::bluetooth::low_energy::Central> request);

  // Called when a AdapterManagerFidlImpl that we own notifies a connection error handler.
  void OnAdapterManagerFidlImplDisconnected(AdapterManagerFidlImpl* adapter_manager_fidl_impl);

  // Called when a LowEnergyCentralFidlImpl that we own notifies its connection error handler.
  void OnLowEnergyCentralFidlImplDisconnected(
      LowEnergyCentralFidlImpl* low_energy_central_fidl_impl);

  // Provides access to the environment. This is used to publish outgoing services.
  std::unique_ptr<app::ApplicationContext> application_context_;

  // Watches for Bluetooth HCI devices and notifies us when adapters get added and removed.
  AdapterManager adapter_manager_;

  // The list of AdapterManager FIDL interface handles that have been vended out.
  std::vector<std::unique_ptr<AdapterManagerFidlImpl>> adapter_manager_fidl_impls_;

  // The list of low_energy::Central FIDL interface handles that have been vended out.
  std::vector<std::unique_ptr<LowEnergyCentralFidlImpl>> low_energy_central_fidl_impls_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  fxl::WeakPtrFactory<App> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace bluetooth_service
