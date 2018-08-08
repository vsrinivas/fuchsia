// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "low_energy_central_server.h"

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "helpers.h"

using fuchsia::bluetooth::ErrorCode;
using fuchsia::bluetooth::Int8;
using fuchsia::bluetooth::Status;

using fuchsia::bluetooth::gatt::Client;
using fuchsia::bluetooth::le::ScanFilterPtr;

namespace bthost {

LowEnergyCentralServer::LowEnergyCentralServer(
    fxl::WeakPtr<::btlib::gap::Adapter> adapter,
    fidl::InterfaceRequest<Central> request, fbl::RefPtr<GattHost> gatt_host)
    : AdapterServerBase(adapter, this, std::move(request)),
      gatt_host_(gatt_host),
      requesting_scan_(false),
      weak_ptr_factory_(this) {
  FXL_DCHECK(gatt_host_);
}

LowEnergyCentralServer::~LowEnergyCentralServer() {
  gatt_host_->UnbindGattClient(reinterpret_cast<GattHost::Token>(this));
}

void LowEnergyCentralServer::GetPeripherals(
    ::fidl::VectorPtr<::fidl::StringPtr> service_uuids,
    GetPeripheralsCallback callback) {
  // TODO:
  FXL_NOTIMPLEMENTED();
}

void LowEnergyCentralServer::GetPeripheral(::fidl::StringPtr identifier,
                                           GetPeripheralCallback callback) {
  // TODO:
  FXL_NOTIMPLEMENTED();
}

void LowEnergyCentralServer::StartScan(ScanFilterPtr filter,
                                       StartScanCallback callback) {
  FXL_VLOG(1) << "Low Energy Central StartScan()";

  if (requesting_scan_) {
    FXL_VLOG(1) << "Scan request already in progress";
    callback(fidl_helpers::NewFidlError(ErrorCode::IN_PROGRESS,
                                        "Scan request in progress"));
    return;
  }

  if (filter && !fidl_helpers::IsScanFilterValid(*filter)) {
    FXL_VLOG(1) << "Invalid scan filter given";
    callback(fidl_helpers::NewFidlError(ErrorCode::INVALID_ARGUMENTS,
                                        "ScanFilter contains an invalid UUID"));
    return;
  }

  if (scan_session_) {
    // A scan is already in progress. Update its filter and report success.
    scan_session_->filter()->Reset();
    fidl_helpers::PopulateDiscoveryFilter(*filter, scan_session_->filter());
    callback(Status());
    return;
  }

  requesting_scan_ = true;
  adapter()->le_discovery_manager()->StartDiscovery(
      [self = weak_ptr_factory_.GetWeakPtr(), filter = std::move(filter),
       callback = std::move(callback)](auto session) {
        if (!self)
          return;

        self->requesting_scan_ = false;

        if (!session) {
          FXL_VLOG(1) << "Failed to start discovery session";
          callback(fidl_helpers::NewFidlError(
              ErrorCode::FAILED, "Failed to start discovery session"));
          return;
        }

        // Assign the filter contents if a filter was provided.
        if (filter)
          fidl_helpers::PopulateDiscoveryFilter(*filter, session->filter());

        session->SetResultCallback([self](const auto& device) {
          if (self)
            self->OnScanResult(device);
        });

        session->set_error_callback([self] {
          if (self) {
            // Clean up the session and notify the delegate.
            self->StopScan();
          }
        });

        self->scan_session_ = std::move(session);
        self->NotifyScanStateChanged(true);
        callback(Status());
      });
}

void LowEnergyCentralServer::StopScan() {
  FXL_VLOG(1) << "Low Energy Central StopScan()";

  if (!scan_session_) {
    FXL_VLOG(1) << "No active discovery session; nothing to do";
    return;
  }

  scan_session_ = nullptr;
  NotifyScanStateChanged(false);
}

void LowEnergyCentralServer::ConnectPeripheral(
    ::fidl::StringPtr identifier,
    ::fidl::InterfaceRequest<Client> client_request,
    ConnectPeripheralCallback callback) {
  FXL_VLOG(1) << "Low Energy Central ConnectPeripheral()";

  auto iter = connections_.find(identifier);
  if (iter != connections_.end()) {
    if (iter->second) {
      callback(fidl_helpers::NewFidlError(
          ErrorCode::ALREADY, "Already connected to requested peer"));
    } else {
      callback(fidl_helpers::NewFidlError(ErrorCode::IN_PROGRESS,
                                          "Connect request pending"));
    }
    return;
  }

  auto self = weak_ptr_factory_.GetWeakPtr();
  auto conn_cb = [self, callback = callback.share(), peer_id = identifier.get(),
                  request = std::move(client_request)](auto status,
                                                       auto conn_ref) mutable {
    if (!self)
      return;

    auto iter = self->connections_.find(peer_id);
    if (iter == self->connections_.end()) {
      FXL_VLOG(1) << "Connect request canceled";
      auto error = fidl_helpers::NewFidlError(ErrorCode::FAILED,
                                              "Connect request canceled");
      callback(std::move(error));
      return;
    }

    if (!status) {
      FXL_DCHECK(!conn_ref);
      auto msg = fxl::StringPrintf("Failed to connect to device (id: %s)",
                                   peer_id.c_str());
      FXL_VLOG(1) << msg;

      callback(fidl_helpers::StatusToFidl(status, std::move(msg)));
      return;
    }

    FXL_DCHECK(conn_ref);
    FXL_DCHECK(peer_id == conn_ref->device_identifier());

    if (iter->second) {
      // This can happen if a connect is requested after a previous request was
      // canceled (e.g. if ConnectPeripheral, DisconnectPeripheral,
      // ConnectPeripheral are called in quick succession). In this case we
      // don't claim |conn_ref| since we already have a reference for this
      // peripheral.
      FXL_VLOG(2) << "Dropping extra connection ref due to previously canceled "
                     "connection attempt";
    }

    uintptr_t token = reinterpret_cast<GattHost::Token>(self.get());
    self->gatt_host_->BindGattClient(token, peer_id, std::move(request));

    conn_ref->set_closed_callback([self, token, peer_id] {
      if (self && self->connections_.erase(peer_id) != 0) {
        self->gatt_host_->UnbindGattClient(token);
        self->NotifyPeripheralDisconnected(peer_id);
      }
    });

    iter->second = std::move(conn_ref);
    callback(Status());
  };

  if (!adapter()->le_connection_manager()->Connect(identifier.get(),
                                                   std::move(conn_cb))) {
    auto msg = fxl::StringPrintf("Cannot connect to unknown device id: %s",
                                 identifier.get().c_str());
    FXL_VLOG(1) << msg;
    callback(fidl_helpers::NewFidlError(ErrorCode::NOT_FOUND, msg));
    return;
  }

  connections_[identifier] = nullptr;
}

void LowEnergyCentralServer::DisconnectPeripheral(
    ::fidl::StringPtr identifier, DisconnectPeripheralCallback callback) {
  auto iter = connections_.find(identifier.get());
  if (iter == connections_.end()) {
    auto msg = fxl::StringPrintf("Client not connected to device (id: %s)",
                                 identifier.get().c_str());
    FXL_VLOG(1) << msg;
    callback(fidl_helpers::NewFidlError(ErrorCode::NOT_FOUND, msg));
    return;
  }

  // If a request to this device is pending then the request will be canceled.
  bool was_pending = !iter->second;
  connections_.erase(iter);

  if (was_pending) {
    FXL_VLOG(1) << "Canceling ConnectPeripheral";
  } else {
    const std::string& peer_id = identifier.get();
    gatt_host_->UnbindGattClient(reinterpret_cast<uintptr_t>(this));
    NotifyPeripheralDisconnected(peer_id);
  }

  callback(Status());
}

void LowEnergyCentralServer::OnScanResult(
    const ::btlib::gap::RemoteDevice& remote_device) {
  auto fidl_device = fidl_helpers::NewLERemoteDevice(remote_device);
  if (!fidl_device) {
    FXL_VLOG(1) << "Ignoring malformed scan result";
    return;
  }

  if (remote_device.rssi() != ::btlib::hci::kRSSIInvalid) {
    fidl_device->rssi = Int8::New();
    fidl_device->rssi->value = remote_device.rssi();
  }

  binding()->events().OnDeviceDiscovered(std::move(*fidl_device));
}

void LowEnergyCentralServer::NotifyScanStateChanged(bool scanning) {
  binding()->events().OnScanStateChanged(scanning);
}

void LowEnergyCentralServer::NotifyPeripheralDisconnected(
    const std::string& identifier) {
  binding()->events().OnPeripheralDisconnected(identifier);
}

}  // namespace bthost
