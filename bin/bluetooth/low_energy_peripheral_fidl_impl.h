// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/bin/bluetooth/adapter_manager.h"
#include "garnet/drivers/bluetooth/lib/gap/low_energy_advertising_manager.h"
#include "garnet/drivers/bluetooth/lib/gap/low_energy_connection_manager.h"
#include "lib/bluetooth/fidl/low_energy.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace bluetooth_service {

// Implements the low_energy::Central FIDL interface.
class LowEnergyPeripheralFidlImpl : public ::bluetooth::low_energy::Peripheral,
                                    public AdapterManager::Observer {
 public:
  // |adapter_manager| is used to lazily request a handle to the corresponding
  // adapter. It MUST out-live this LowEnergyCentralFidlImpl instance.
  using ConnectionErrorHandler =
      std::function<void(LowEnergyPeripheralFidlImpl*)>;
  LowEnergyPeripheralFidlImpl(
      AdapterManager* adapter_manager,
      ::fidl::InterfaceRequest<::bluetooth::low_energy::Peripheral> request,
      const ConnectionErrorHandler& connection_error_handler);
  ~LowEnergyPeripheralFidlImpl() override;

 private:
  // ::bluetooth::low_energy::Peripheral overrides:
  void StartAdvertising(
      ::bluetooth::low_energy::AdvertisingDataPtr advertising_data,
      ::bluetooth::low_energy::AdvertisingDataPtr scan_result,
      ::fidl::InterfaceHandle<::bluetooth::low_energy::PeripheralDelegate>
          delegate,
      uint32_t interval,
      bool anonymous,
      const StartAdvertisingCallback& callback) override;

  void StopAdvertising(const ::fidl::String& advertisement_id,
                       const StopAdvertisingCallback& callback) override;

  // AdapterManager::Delegate overrides:
  void OnActiveAdapterChanged(bluetooth::gap::Adapter* adapter) override;

  // Called when a central connects to us.  When this is called, the
  // advertisement in |advertisement_id| has been stopped.
  void OnConnected(std::string advertisement_id,
                   ::bluetooth::gap::LowEnergyConnectionRefPtr connection);

  // Helper function to the current advertisement manager or nullptr.
  bluetooth::gap::LowEnergyAdvertisingManager* GetAdvertisingManager() const;

  // We keep a raw pointer as we expect this to outlive us.
  AdapterManager* adapter_manager_;  // weak

  // The interface binding that represents the connection to the client
  // application.
  ::fidl::Binding<::bluetooth::low_energy::Peripheral> binding_;

  // Tracks currently active advertisements.
  // Keys are advertisement ids, delegates are bound if they are connectable.
  std::unordered_map<std::string,
                     ::bluetooth::low_energy::PeripheralDelegatePtr>
      delegates_;

  // Keep this as the last member to make sure that all weak pointers are
  // invalidated before other members get destroyed.
  fxl::WeakPtrFactory<LowEnergyPeripheralFidlImpl> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LowEnergyPeripheralFidlImpl);
};

}  // namespace bluetooth_service
