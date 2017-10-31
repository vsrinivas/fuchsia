// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "low_energy_peripheral_fidl_impl.h"

#include "garnet/drivers/bluetooth/lib/gap/advertising_data.h"
#include "garnet/drivers/bluetooth/lib/gap/remote_device.h"
#include "garnet/drivers/bluetooth/lib/hci/hci_constants.h"
#include "garnet/drivers/bluetooth/lib/hci/util.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"

#include "app.h"
#include "fidl_helpers.h"

// The internal library components and the generated FIDL bindings are both
// declared under the "bluetooth" namespace. We define an alias here to
// disambiguate.
namespace btfidl = ::bluetooth;

namespace bluetooth_service {

namespace {

std::string ErrorToString(btfidl::hci::Status error) {
  switch (error) {
    case ::btfidl::hci::kSuccess:
      return "Success";
    case ::btfidl::hci::kConnectionLimitExceeded:
      return "Maximum advertisement amount reached";
    case ::btfidl::hci::kMemoryCapacityExceeded:
      return "Advertisement exceeds maximum allowed length";
    default:
      return ::btfidl::hci::StatusToString(error);
  }
}

}  // namespace

LowEnergyPeripheralFidlImpl::LowEnergyPeripheralFidlImpl(
    AdapterManager* adapter_manager,
    ::fidl::InterfaceRequest<::btfidl::low_energy::Peripheral> request,
    const ConnectionErrorHandler& connection_error_handler)
    : adapter_manager_(adapter_manager),
      binding_(this, std::move(request)),
      weak_ptr_factory_(this) {
  adapter_manager_->AddObserver(this);
  binding_.set_connection_error_handler(
      [this, connection_error_handler] { connection_error_handler(this); });
}

LowEnergyPeripheralFidlImpl::~LowEnergyPeripheralFidlImpl() {
  adapter_manager_->RemoveObserver(this);
  // Stop all the advertisements that this client has started
  auto advertising_manager = GetAdvertisingManager();
  if (!advertising_manager)
    return;
  for (const auto& it : delegates_) {
    advertising_manager->StopAdvertising(it.first);
  }
}

void LowEnergyPeripheralFidlImpl::StartAdvertising(
    ::btfidl::low_energy::AdvertisingDataPtr advertising_data,
    ::btfidl::low_energy::AdvertisingDataPtr scan_result,
    ::fidl::InterfaceHandle<::btfidl::low_energy::PeripheralDelegate> delegate,
    uint32_t interval,
    bool anonymous,
    const StartAdvertisingCallback& callback) {
  auto advertising_manager = GetAdvertisingManager();
  if (!advertising_manager) {
    auto fidlerror = fidl_helpers::NewErrorStatus(
        ::btfidl::ErrorCode::BLUETOOTH_NOT_AVAILABLE, "Not available");
    callback(std::move(fidlerror), "");
    return;
  }
  bluetooth::gap::AdvertisingData ad_data, scan_data;
  bluetooth::gap::AdvertisingData::FromFidl(advertising_data, &ad_data);
  if (scan_result) {
    bluetooth::gap::AdvertisingData::FromFidl(scan_result, &scan_data);
  }
  bluetooth::gap::LowEnergyAdvertisingManager::ConnectionCallback connect_cb;
  if (delegate) {
    connect_cb =
        fbl::BindMember(this, &LowEnergyPeripheralFidlImpl::OnConnected);
  }
  // |delegate| is temporarily held by the result callback, which will close the
  // delegate channel if the advertising fails (after returning the status)
  auto advertising_result_cb = fxl::MakeCopyable(
      [self = weak_ptr_factory_.GetWeakPtr(), callback,
       delegate = std::move(delegate)](std::string advertisement_id,
                                       ::btfidl::hci::Status status) mutable {
        if (!self)
          return;

        if (status != ::btfidl::hci::kSuccess) {
          auto fidlerror = fidl_helpers::NewErrorStatus(
              ::btfidl::ErrorCode::PROTOCOL_ERROR, ErrorToString(status));
          fidlerror->error->protocol_error_code = status;
          callback(std::move(fidlerror), "");
          return;
        }

        // This will be an unbound interface pointer if there's no delegate, but
        // we keep it to track the current advertisements.
        auto delegate_ptr = ::btfidl::low_energy::PeripheralDelegatePtr::Create(
            std::move(delegate));
        self->delegates_[advertisement_id] = std::move(delegate_ptr);
        callback(::btfidl::Status::New(), advertisement_id);
      });
  bool result = advertising_manager->StartAdvertising(
      ad_data, scan_data, connect_cb, interval, anonymous,
      advertising_result_cb);
  if (!result) {
    auto fidlerror =
        fidl_helpers::NewErrorStatus(::btfidl::ErrorCode::INVALID_ARGUMENTS,
                                     "Can't advertise connectable anonymously");
    callback(std::move(fidlerror), "");
  }
}

void LowEnergyPeripheralFidlImpl::StopAdvertising(
    const ::fidl::String& advertisement_id,
    const StopAdvertisingCallback& callback) {
  delegates_.erase(advertisement_id);
  auto advertising_manager = GetAdvertisingManager();
  if (!advertising_manager) {
    callback(fidl_helpers::NewErrorStatus(
        ::btfidl::ErrorCode::BLUETOOTH_NOT_AVAILABLE, "Not available"));
    return;
  }
  advertising_manager->StopAdvertising(advertisement_id);
  callback(::btfidl::Status::New());
}

void LowEnergyPeripheralFidlImpl::OnActiveAdapterChanged(
    bluetooth::gap::Adapter* adapter) {
  // TODO(jamuraa): re-add the advertisements that have been started here?
  FXL_NOTIMPLEMENTED();
}

void LowEnergyPeripheralFidlImpl::OnConnected(
    std::string advertisement_id,
    bluetooth::gap::LowEnergyConnectionRefPtr connection) {
  auto it = delegates_.find(advertisement_id);
  if (it == delegates_.end() || !it->second)
    return;

  auto* device =
      adapter_manager_->GetActiveAdapter()->device_cache().FindDeviceById(
          connection->device_identifier());

  auto fidl_device = fidl_helpers::NewLERemoteDevice(*device);
  it->second->OnCentralConnected(advertisement_id, std::move(fidl_device));
}

bluetooth::gap::LowEnergyAdvertisingManager*
LowEnergyPeripheralFidlImpl::GetAdvertisingManager() const {
  auto adapter = adapter_manager_->GetActiveAdapter();
  if (!adapter)
    return nullptr;
  return adapter->le_advertising_manager();
}

}  // namespace bluetooth_service
