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

// Make the FIDL namespace explicit.
namespace btfidl = ::bluetooth;

namespace bluetooth_service {

namespace {

std::string ErrorToString(btlib::hci::Status error) {
  switch (error) {
    case ::btlib::hci::kSuccess:
      return "Success";
    case ::btlib::hci::kConnectionLimitExceeded:
      return "Maximum advertisement amount reached";
    case ::btlib::hci::kMemoryCapacityExceeded:
      return "Advertisement exceeds maximum allowed length";
    default:
      return ::btlib::hci::StatusToString(error);
  }
}

}  // namespace

LowEnergyPeripheralFidlImpl::InstanceData::InstanceData(const std::string& id,
                                                        DelegatePtr delegate)
    : id_(id), delegate_(std::move(delegate)) {}

void LowEnergyPeripheralFidlImpl::InstanceData::RetainConnection(
    ConnectionRefPtr conn_ref,
    ::btfidl::low_energy::RemoteDevicePtr peer) {
  FXL_DCHECK(connectable());
  FXL_DCHECK(!conn_ref_);

  conn_ref_ = std::move(conn_ref);
  delegate_->OnCentralConnected(id_, std::move(peer));
}

void LowEnergyPeripheralFidlImpl::InstanceData::ReleaseConnection() {
  FXL_DCHECK(connectable());
  FXL_DCHECK(conn_ref_);

  delegate_->OnCentralDisconnected(conn_ref_->device_identifier());
  conn_ref_ = nullptr;
}

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

  for (const auto& it : instances_) {
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

  ::btlib::gap::AdvertisingData ad_data, scan_data;
  ::btlib::gap::AdvertisingData::FromFidl(advertising_data, &ad_data);
  if (scan_result) {
    ::btlib::gap::AdvertisingData::FromFidl(scan_result, &scan_data);
  }

  auto self = weak_ptr_factory_.GetWeakPtr();

  ::btlib::gap::LowEnergyAdvertisingManager::ConnectionCallback connect_cb;
  if (delegate) {
    // TODO(armansito): The conversion from hci::Connection to
    // gap::LowEnergyConnectionRef should be performed by a gap library object
    // and not in this layer (see NET-355).
    connect_cb = [self](auto adv_id, auto link) {
      if (self)
        self->OnConnected(std::move(adv_id), std::move(link));
    };
  }
  // |delegate| is temporarily held by the result callback, which will close the
  // delegate channel if the advertising fails (after returning the status)
  auto advertising_result_cb = fxl::MakeCopyable(
      [self, callback, delegate = std::move(delegate)](
          std::string advertisement_id, ::btlib::hci::Status status) mutable {
        if (!self)
          return;

        if (status != ::btlib::hci::kSuccess) {
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
        self->instances_[advertisement_id] =
            InstanceData(advertisement_id, std::move(delegate_ptr));
        callback(::btfidl::Status::New(), advertisement_id);
      });

  advertising_manager->StartAdvertising(ad_data, scan_data, connect_cb,
                                        interval, anonymous,
                                        advertising_result_cb);
}

void LowEnergyPeripheralFidlImpl::StopAdvertising(
    const ::fidl::String& advertisement_id,
    const StopAdvertisingCallback& callback) {
  auto advertising_manager = GetAdvertisingManager();
  if (!advertising_manager) {
    callback(fidl_helpers::NewErrorStatus(
        ::btfidl::ErrorCode::BLUETOOTH_NOT_AVAILABLE, "Not available"));
    return;
  }

  auto iter = instances_.find(advertisement_id);
  if (iter == instances_.end()) {
    callback(fidl_helpers::NewErrorStatus(::btfidl::ErrorCode::NOT_FOUND,
                                          "Unrecognized advertisement ID"));
    return;
  }

  instances_.erase(iter);
  advertising_manager->StopAdvertising(advertisement_id);

  callback(::btfidl::Status::New());
}

void LowEnergyPeripheralFidlImpl::OnActiveAdapterChanged(
    ::btlib::gap::Adapter* adapter) {
  // TODO(jamuraa): re-add the advertisements that have been started here?
  // TODO(armansito): Stop advertisements started using the old active adapter.

  // Clean up all connections and advertising instances.
  instances_.clear();
}

void LowEnergyPeripheralFidlImpl::OnConnected(
    std::string advertisement_id,
    ::btlib::hci::ConnectionPtr link) {
  FXL_DCHECK(link);

  // If the active adapter that was used to start advertising was changed before
  // we process this connection then the instance will have been removed.
  auto it = instances_.find(advertisement_id);
  if (it == instances_.end()) {
    FXL_VLOG(1) << "Connection received from wrong advertising instance";
    return;
  }

  FXL_DCHECK(it->second.connectable());

  auto adapter = adapter_manager_->GetActiveAdapter();
  if (!adapter) {
    FXL_VLOG(1) << "Adapter removed: ignoring connection";
    return;
  }

  auto conn = adapter->le_connection_manager()->RegisterRemoteInitiatedLink(
      std::move(link));
  if (!conn) {
    FXL_VLOG(1) << "Incoming connection rejected";
    return;
  }

  auto self = weak_ptr_factory_.GetWeakPtr();
  conn->set_closed_callback([self, id = advertisement_id] {
    FXL_VLOG(1) << "Central disconnected";

    if (!self)
      return;

    // Make sure that the instance hasn't been removed.
    auto it = self->instances_.find(id);
    if (it == self->instances_.end())
      return;

    // This sends OnCentralDisconnected() to the delegate.
    it->second.ReleaseConnection();
  });

  // A RemoteDevice will have been created for the new connection.
  auto* device =
      adapter->device_cache().FindDeviceById(conn->device_identifier());
  FXL_DCHECK(device);

  FXL_VLOG(1) << "Central connected";
  it->second.RetainConnection(std::move(conn),
                              fidl_helpers::NewLERemoteDevice(*device));
}

::btlib::gap::LowEnergyAdvertisingManager*
LowEnergyPeripheralFidlImpl::GetAdvertisingManager() const {
  auto adapter = adapter_manager_->GetActiveAdapter();
  if (!adapter)
    return nullptr;
  return adapter->le_advertising_manager();
}

}  // namespace bluetooth_service
