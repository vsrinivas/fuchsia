// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "low_energy_central_fidl_impl.h"

#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"

#include "app.h"
#include "fidl_helpers.h"

// The internal library components and the generated FIDL bindings are both declared under the
// "bluetooth" namespace. We define an alias here to disambiguate.
namespace btfidl = ::bluetooth;

namespace bluetooth_service {

LowEnergyCentralFidlImpl::LowEnergyCentralFidlImpl(
    AdapterManager* adapter_manager,
    ::fidl::InterfaceRequest<::btfidl::low_energy::Central> request,
    const ConnectionErrorHandler& connection_error_handler)
    : adapter_manager_(adapter_manager),
      requesting_scan_(false),
      binding_(this, std::move(request)),
      weak_ptr_factory_(this) {
  FXL_DCHECK(adapter_manager_);
  FXL_DCHECK(connection_error_handler);
  adapter_manager_->AddObserver(this);
  binding_.set_connection_error_handler(
      [this, connection_error_handler] { connection_error_handler(this); });
}

LowEnergyCentralFidlImpl::~LowEnergyCentralFidlImpl() {
  adapter_manager_->RemoveObserver(this);
}

void LowEnergyCentralFidlImpl::SetDelegate(
    ::fidl::InterfaceHandle<::btfidl::low_energy::CentralDelegate> delegate) {
  if (!delegate) {
    FXL_LOG(ERROR) << "Cannot set a null delegate";
    return;
  }

  delegate_ = ::btfidl::low_energy::CentralDelegatePtr::Create(std::move(delegate));
  delegate_.set_connection_error_handler([this] {
    FXL_LOG(INFO) << "LowEnergyCentral delegate disconnected";
    delegate_ = nullptr;
  });
}

void LowEnergyCentralFidlImpl::GetPeripherals(::fidl::Array<::fidl::String> service_uuids,
                                              const GetPeripheralsCallback& callback) {
  // TODO:
  FXL_NOTIMPLEMENTED();
}

void LowEnergyCentralFidlImpl::GetPeripheral(const ::fidl::String& identifier,
                                             const GetPeripheralCallback& callback) {
  // TODO:
  FXL_NOTIMPLEMENTED();
}

void LowEnergyCentralFidlImpl::StartScan(::btfidl::low_energy::ScanFilterPtr filter,
                                         const StartScanCallback& callback) {
  FXL_LOG(INFO) << "Low Energy Central StartScan()";

  if (!adapter_manager_->GetActiveAdapter()) {
    FXL_LOG(ERROR) << "Adapter not available";
    callback(fidl_helpers::NewErrorStatus(::btfidl::ErrorCode::BLUETOOTH_NOT_AVAILABLE,
                                          "Bluetooth not available on the current system"));
    return;
  }

  if (requesting_scan_) {
    FXL_LOG(ERROR) << "Scan request already in progress";
    callback(
        fidl_helpers::NewErrorStatus(::btfidl::ErrorCode::IN_PROGRESS, "Scan request in progress"));
    return;
  }

  if (filter && !fidl_helpers::IsScanFilterValid(*filter)) {
    FXL_LOG(ERROR) << "Invalid scan filter given";
    callback(fidl_helpers::NewErrorStatus(::btfidl::ErrorCode::INVALID_ARGUMENTS,
                                          "ScanFilter contains an invalid UUID"));
    return;
  }

  if (scan_session_) {
    // A scan is already in progress. Update its filter and report success.
    scan_session_->ResetToDefault();
    fidl_helpers::PopulateDiscoveryFilter(*filter, scan_session_->filter());
    callback(::btfidl::Status::New());
    return;
  }

  requesting_scan_ = true;
  adapter_manager_->GetActiveAdapter()->le_discovery_manager()->StartDiscovery(fxl::MakeCopyable([
    self = weak_ptr_factory_.GetWeakPtr(), filter = std::move(filter), callback
  ](auto session) {
    if (!self) return;

    self->requesting_scan_ = false;

    if (!session) {
      FXL_LOG(ERROR) << "Failed to start discovery session";
      callback(fidl_helpers::NewErrorStatus(::btfidl::ErrorCode::FAILED,
                                            "Failed to start discovery session"));
      return;
    }

    // Assign the filter contents if a filter was provided.
    if (filter) fidl_helpers::PopulateDiscoveryFilter(*filter, session->filter());

    session->SetResultCallback([self](const auto& device) {
      if (self) self->OnScanResult(device);
    });

    self->scan_session_ = std::move(session);
    self->NotifyScanStateChanged(true);
    callback(::btfidl::Status::New());
  }));
}

void LowEnergyCentralFidlImpl::StopScan() {
  FXL_LOG(INFO) << "Low Energy Central StopScan()";

  if (!scan_session_) {
    FXL_LOG(WARNING) << "No active discovery session; nothing to do";
    return;
  }

  scan_session_ = nullptr;
  NotifyScanStateChanged(false);
}

void LowEnergyCentralFidlImpl::OnActiveAdapterChanged(bluetooth::gap::Adapter* adapter) {
  FXL_LOG(INFO) << "The active adapter has changed; terminating all running LE Central procedures";

  if (scan_session_) StopScan();
}

void LowEnergyCentralFidlImpl::OnScanResult(const ::bluetooth::gap::RemoteDevice& remote_device) {
  if (!delegate_) return;

  auto fidl_device = fidl_helpers::NewLERemoteDevice(remote_device);
  if (!fidl_device) {
    FXL_LOG(WARNING) << "Ignoring malformed scan result";
    return;
  }

  if (remote_device.rssi() != ::bluetooth::hci::kRSSIInvalid) {
    fidl_device->rssi = ::btfidl::Int8::New();
    fidl_device->rssi->value = remote_device.rssi();
  }

  delegate_->OnDeviceDiscovered(std::move(fidl_device));
}

void LowEnergyCentralFidlImpl::NotifyScanStateChanged(bool scanning) {
  if (!delegate_) return;

  delegate_->OnScanStateChanged(scanning);
}

}  // namespace bluetooth_service
