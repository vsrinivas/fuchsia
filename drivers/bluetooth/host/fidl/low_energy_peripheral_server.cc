// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "low_energy_peripheral_server.h"

#include "garnet/drivers/bluetooth/lib/gap/advertising_data.h"
#include "garnet/drivers/bluetooth/lib/gap/remote_device.h"
#include "garnet/drivers/bluetooth/lib/hci/hci_constants.h"
#include "garnet/drivers/bluetooth/lib/hci/util.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"

#include "helpers.h"

using bluetooth::ErrorCode;
using bluetooth::Status;

using bluetooth::low_energy::AdvertisingData;
using bluetooth::low_energy::AdvertisingDataPtr;
using bluetooth::low_energy::Peripheral;
using bluetooth::low_energy::PeripheralDelegate;
using bluetooth::low_energy::PeripheralDelegatePtr;
using bluetooth::low_energy::RemoteDevicePtr;

namespace bthost {

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

LowEnergyPeripheralServer::InstanceData::InstanceData(const std::string& id,
                                                      DelegatePtr delegate)
    : id_(id), delegate_(std::move(delegate)) {}

void LowEnergyPeripheralServer::InstanceData::RetainConnection(
    ConnectionRefPtr conn_ref,
    RemoteDevicePtr peer) {
  FXL_DCHECK(connectable());
  FXL_DCHECK(!conn_ref_);

  conn_ref_ = std::move(conn_ref);
  delegate_->OnCentralConnected(id_, std::move(peer));
}

void LowEnergyPeripheralServer::InstanceData::ReleaseConnection() {
  FXL_DCHECK(connectable());
  FXL_DCHECK(conn_ref_);

  delegate_->OnCentralDisconnected(conn_ref_->device_identifier());
  conn_ref_ = nullptr;
}

LowEnergyPeripheralServer::LowEnergyPeripheralServer(
    fxl::WeakPtr<::btlib::gap::Adapter> adapter,
    fidl::InterfaceRequest<Peripheral> request)
    : ServerBase(adapter, this, std::move(request)), weak_ptr_factory_(this) {}

LowEnergyPeripheralServer::~LowEnergyPeripheralServer() {
  auto* advertising_manager = adapter()->le_advertising_manager();
  FXL_DCHECK(advertising_manager);

  for (const auto& it : instances_) {
    advertising_manager->StopAdvertising(it.first);
  }
}

void LowEnergyPeripheralServer::StartAdvertising(
    AdvertisingDataPtr advertising_data,
    AdvertisingDataPtr scan_result,
    ::fidl::InterfaceHandle<PeripheralDelegate> delegate,
    uint32_t interval,
    bool anonymous,
    const StartAdvertisingCallback& callback) {
  auto* advertising_manager = adapter()->le_advertising_manager();
  FXL_DCHECK(advertising_manager);

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
          std::string ad_id, ::btlib::hci::Status status) mutable {
        if (!self)
          return;

        if (status != ::btlib::hci::kSuccess) {
          auto fidlerror = fidl_helpers::NewErrorStatus(
              ErrorCode::PROTOCOL_ERROR, ErrorToString(status));
          fidlerror->error->protocol_error_code = status;
          callback(std::move(fidlerror), "");
          return;
        }

        auto delegate_ptr = delegate.Bind();
        delegate_ptr.set_error_handler([self, ad_id] {
          if (self) {
            self->StopAdvertisingInternal(ad_id);
          }
        });

        self->instances_[ad_id] = InstanceData(ad_id, std::move(delegate_ptr));
        callback(Status::New(), ad_id);
      });

  advertising_manager->StartAdvertising(ad_data, scan_data, connect_cb,
                                        interval, anonymous,
                                        advertising_result_cb);
}

void LowEnergyPeripheralServer::StopAdvertising(
    const ::fidl::String& id,
    const StopAdvertisingCallback& callback) {
  if (StopAdvertisingInternal(id)) {
    callback(Status::New());
  } else {
    callback(fidl_helpers::NewErrorStatus(ErrorCode::NOT_FOUND,
                                          "Unrecognized advertisement ID"));
  }
}

bool LowEnergyPeripheralServer::StopAdvertisingInternal(const std::string& id) {
  auto count = instances_.erase(id);
  if (count) {
    adapter()->le_advertising_manager()->StopAdvertising(id);
  }

  return count != 0;
}

void LowEnergyPeripheralServer::OnConnected(std::string advertisement_id,
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

  auto conn = adapter()->le_connection_manager()->RegisterRemoteInitiatedLink(
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
      adapter()->device_cache().FindDeviceById(conn->device_identifier());
  FXL_DCHECK(device);

  FXL_VLOG(1) << "Central connected";
  it->second.RetainConnection(std::move(conn),
                              fidl_helpers::NewLERemoteDevice(*device));
}

}  // namespace bthost
