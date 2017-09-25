// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/bin/bluetooth/adapter_manager.h"
#include "lib/bluetooth/fidl/low_energy.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"

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

  // We keep a raw pointer as we expect this to outlive us.
  AdapterManager* adapter_manager_;  // weak

  // The interface binding that represents the connection to the client
  // application.
  ::fidl::Binding<::bluetooth::low_energy::Peripheral> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LowEnergyPeripheralFidlImpl);
};

}  // namespace bluetooth_service
