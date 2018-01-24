// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "low_energy_central_server.h"

#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "helpers.h"

using bluetooth::ErrorCode;
using bluetooth::Int8;
using bluetooth::Status;

using bluetooth::low_energy::CentralDelegate;
using bluetooth::low_energy::CentralDelegatePtr;
using bluetooth::low_energy::ScanFilterPtr;

namespace bthost {

LowEnergyCentralServer::LowEnergyCentralServer(
    fxl::WeakPtr<::btlib::gap::Adapter> adapter,
    fidl::InterfaceRequest<Central> request)
    : ServerBase(adapter, this, std::move(request)),
      requesting_scan_(false),
      weak_ptr_factory_(this) {}

void LowEnergyCentralServer::SetDelegate(
    ::fidl::InterfaceHandle<CentralDelegate> delegate) {
  if (!delegate) {
    FXL_VLOG(1) << "Cannot set a null delegate";
    return;
  }

  delegate_ = delegate.Bind();
  delegate_.set_error_handler([this] {
    FXL_VLOG(1) << "LowEnergyCentral delegate disconnected";
    delegate_ = nullptr;
  });
}

void LowEnergyCentralServer::GetPeripherals(
    ::fidl::Array<::fidl::String> service_uuids,
    const GetPeripheralsCallback& callback) {
  // TODO:
  FXL_NOTIMPLEMENTED();
}

void LowEnergyCentralServer::GetPeripheral(
    const ::fidl::String& identifier,
    const GetPeripheralCallback& callback) {
  // TODO:
  FXL_NOTIMPLEMENTED();
}

void LowEnergyCentralServer::StartScan(ScanFilterPtr filter,
                                       const StartScanCallback& callback) {
  FXL_VLOG(1) << "Low Energy Central StartScan()";

  if (requesting_scan_) {
    FXL_VLOG(1) << "Scan request already in progress";
    callback(fidl_helpers::NewErrorStatus(ErrorCode::IN_PROGRESS,
                                          "Scan request in progress"));
    return;
  }

  if (filter && !fidl_helpers::IsScanFilterValid(*filter)) {
    FXL_VLOG(1) << "Invalid scan filter given";
    callback(fidl_helpers::NewErrorStatus(
        ErrorCode::INVALID_ARGUMENTS, "ScanFilter contains an invalid UUID"));
    return;
  }

  if (scan_session_) {
    // A scan is already in progress. Update its filter and report success.
    scan_session_->ResetToDefault();
    fidl_helpers::PopulateDiscoveryFilter(*filter, scan_session_->filter());
    callback(Status::New());
    return;
  }

  requesting_scan_ = true;
  adapter()->le_discovery_manager()->StartDiscovery(
      fxl::MakeCopyable([self = weak_ptr_factory_.GetWeakPtr(),
                         filter = std::move(filter), callback](auto session) {
        if (!self)
          return;

        self->requesting_scan_ = false;

        if (!session) {
          FXL_VLOG(1) << "Failed to start discovery session";
          callback(fidl_helpers::NewErrorStatus(
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

        self->scan_session_ = std::move(session);
        self->NotifyScanStateChanged(true);
        callback(Status::New());
      }));
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
    const ::fidl::String& identifier,
    const ConnectPeripheralCallback& callback) {
  FXL_VLOG(1) << "Low Energy Central ConnectPeripheral()";

  auto iter = connections_.find(identifier);
  if (iter != connections_.end()) {
    if (iter->second) {
      callback(fidl_helpers::NewErrorStatus(
          ErrorCode::ALREADY, "Already connected to requested peer"));
    } else {
      callback(fidl_helpers::NewErrorStatus(ErrorCode::IN_PROGRESS,
                                            "Connect request pending"));
    }
    return;
  }

  auto self = weak_ptr_factory_.GetWeakPtr();
  auto conn_cb = [self, callback, id = identifier.get()](auto status,
                                                         auto conn_ref) {
    if (!self)
      return;

    auto iter = self->connections_.find(id);
    if (iter == self->connections_.end()) {
      FXL_VLOG(1) << "Connect request canceled";
      auto error = fidl_helpers::NewErrorStatus(ErrorCode::FAILED,
                                                "Connect request canceled");
      callback(std::move(error));
      return;
    }

    if (status != ::btlib::hci::Status::kSuccess) {
      FXL_DCHECK(!conn_ref);
      auto msg =
          fxl::StringPrintf("Failed to connect to device (id: %s)", id.c_str());
      FXL_VLOG(1) << msg;

      // TODO(armansito): Report PROTOCOL_ERROR only if |status| correspond to
      // an actual HCI error reported from the controller. LE conn mgr currently
      // uses HCI error codes for internal errors which needs to change.
      auto error = fidl_helpers::NewErrorStatus(ErrorCode::PROTOCOL_ERROR, msg);
      error->error->protocol_error_code = status;
      callback(std::move(error));
      return;
    }

    FXL_DCHECK(conn_ref);
    FXL_DCHECK(id == conn_ref->device_identifier());

    if (!iter->second) {
      // This is in response to a pending connect request.
      conn_ref->set_closed_callback([self, id] {
        if (!self)
          return;

        self->connections_.erase(id);
        self->NotifyPeripheralDisconnected(id);
      });
      self->connections_[id] = std::move(conn_ref);
    } else {
      // This can happen if a connect is requested after a previous request was
      // canceled (e.g. if ConnectPeripheral, DisconnectPeripheral,
      // ConnectPeripheral are called in quick succession). In this case we
      // don't claim |conn_ref| since we already have a reference for this
      // peripheral.
      FXL_VLOG(2) << "Dropping extra connection ref due to previously canceled "
                     "connection attempt";
    }

    callback(Status::New());
  };

  if (!adapter()->le_connection_manager()->Connect(identifier.get(), conn_cb)) {
    auto msg = fxl::StringPrintf("Cannot connect to unknown device id: %s",
                                 identifier.get().c_str());
    FXL_VLOG(1) << msg;
    callback(fidl_helpers::NewErrorStatus(ErrorCode::NOT_FOUND, msg));
    return;
  }

  connections_[identifier] = nullptr;
}

void LowEnergyCentralServer::DisconnectPeripheral(
    const ::fidl::String& identifier,
    const DisconnectPeripheralCallback& callback) {
  auto iter = connections_.find(identifier.get());
  if (iter == connections_.end()) {
    auto msg = fxl::StringPrintf("Client not connected to device (id: %s)",
                                 identifier.get().c_str());
    FXL_VLOG(1) << msg;
    callback(fidl_helpers::NewErrorStatus(ErrorCode::NOT_FOUND, msg));
    return;
  }

  // If a request to this device is pending then the request will be canceled.
  bool was_pending = !iter->second;
  connections_.erase(iter);

  if (was_pending) {
    FXL_VLOG(1) << "Canceling ConnectPeripheral";
  } else {
    NotifyPeripheralDisconnected(identifier.get());
  }

  callback(Status::New());
}

void LowEnergyCentralServer::OnScanResult(
    const ::btlib::gap::RemoteDevice& remote_device) {
  if (!delegate_)
    return;

  auto fidl_device = fidl_helpers::NewLERemoteDevice(remote_device);
  if (!fidl_device) {
    FXL_VLOG(1) << "Ignoring malformed scan result";
    return;
  }

  if (remote_device.rssi() != ::btlib::hci::kRSSIInvalid) {
    fidl_device->rssi = Int8::New();
    fidl_device->rssi->value = remote_device.rssi();
  }

  delegate_->OnDeviceDiscovered(std::move(fidl_device));
}

void LowEnergyCentralServer::NotifyScanStateChanged(bool scanning) {
  if (delegate_)
    delegate_->OnScanStateChanged(scanning);
}

void LowEnergyCentralServer::NotifyPeripheralDisconnected(
    const std::string& identifier) {
  if (delegate_)
    delegate_->OnPeripheralDisconnected(identifier);
}

}  // namespace bthost
