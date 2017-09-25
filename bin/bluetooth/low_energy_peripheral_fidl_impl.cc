// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "low_energy_peripheral_fidl_impl.h"

#include "lib/fxl/logging.h"

#include "app.h"
#include "fidl_helpers.h"

// The internal library components and the generated FIDL bindings are both
// declared under the "bluetooth" namespace. We define an alias here to
// disambiguate.
namespace btfidl = ::bluetooth;

namespace bluetooth_service {

LowEnergyPeripheralFidlImpl::LowEnergyPeripheralFidlImpl(
    AdapterManager* adapter_manager,
    ::fidl::InterfaceRequest<::btfidl::low_energy::Peripheral> request,
    const ConnectionErrorHandler& connection_error_handler)
    : adapter_manager_(adapter_manager), binding_(this, std::move(request)) {
  adapter_manager_->AddObserver(this);
  binding_.set_connection_error_handler(
      [this, connection_error_handler] { connection_error_handler(this); });
}

LowEnergyPeripheralFidlImpl::~LowEnergyPeripheralFidlImpl() {
  adapter_manager_->RemoveObserver(this);
}

void LowEnergyPeripheralFidlImpl::StartAdvertising(
    ::btfidl::low_energy::AdvertisingDataPtr advertising_data,
    ::btfidl::low_energy::AdvertisingDataPtr scan_result,
    ::fidl::InterfaceHandle<::btfidl::low_energy::PeripheralDelegate> delegate,
    uint32_t interval,
    bool anonymous,
    const StartAdvertisingCallback& callback) {
  // TODO(jamuraa)
  FXL_NOTIMPLEMENTED();
}

void LowEnergyPeripheralFidlImpl::StopAdvertising(
    const ::fidl::String& advertisement_id,
    const StopAdvertisingCallback& callback) {
  // TODO(jamuraa)
  FXL_NOTIMPLEMENTED();
}

// AdapterManager::Delegate overrides:
void LowEnergyPeripheralFidlImpl::OnActiveAdapterChanged(
    bluetooth::gap::Adapter* adapter) {
  // TODO(jamuraa): re-add the advertisements that have been started here?
  FXL_NOTIMPLEMENTED();
}

}  // namespace bluetooth_service
