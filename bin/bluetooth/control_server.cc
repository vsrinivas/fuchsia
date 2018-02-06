// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "control_server.h"

#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"

using bluetooth::Error;
using bluetooth::ErrorCode;
using bluetooth::Status;
using bluetooth_control::AdapterInfo;
using bluetooth_control::AdapterInfoPtr;
using bluetooth_control::Control;
using bluetooth_control::ControlDelegate;
using bluetooth_control::ControlDelegatePtr;
using bluetooth_control::ControlPtr;
using bluetooth_control::PairingDelegate;
using bluetooth_control::PairingDelegatePtr;

namespace bluetooth_service {

ControlServer::ControlServer(
    ::bluetooth_service::BluetoothManager* bluetooth_manager,
    ::fidl::InterfaceRequest<Control> request,
    const ConnectionErrorHandler& connection_error_handler)
    : bluetooth_manager_(bluetooth_manager),
      binding_(this, std::move(request)),
      weak_ptr_factory_(this) {
  FXL_DCHECK(bluetooth_manager_);
  FXL_DCHECK(connection_error_handler);
  binding_.set_error_handler(
      [this, connection_error_handler] { connection_error_handler(this); });
}

void ControlServer::NotifyActiveAdapterChanged(
    const AdapterInfoPtr& adapter_ptr) {
  if (delegate_) {
    if (!adapter_ptr) {
      delegate_->OnActiveAdapterChanged(nullptr);
      return;
    }

    auto adapter_cpy = AdapterInfo::New();
    adapter_ptr->Clone(adapter_cpy.get());
    delegate_->OnActiveAdapterChanged(std::move(adapter_cpy));
  }
}

void ControlServer::NotifyAdapterUpdated(const AdapterInfoPtr& adapter_ptr) {
  if (delegate_) {
    AdapterInfo adapter;
    adapter_ptr->Clone(&adapter);
    delegate_->OnAdapterUpdated(std::move(adapter));
  }
}

void ControlServer::NotifyAdapterRemoved(const std::string& adapter_id) {
  if (delegate_) {
    delegate_->OnAdapterRemoved(adapter_id);
  }
}

void ControlServer::NotifyRemoteDeviceUpdated(
    const bluetooth_control::RemoteDevice& device) {
  if (device_delegate_) {
    bluetooth_control::RemoteDevice dev_cpy;
    device.Clone(&dev_cpy);
    device_delegate_->OnDeviceUpdated(std::move(dev_cpy));
  }
}

void ControlServer::IsBluetoothAvailable(
    IsBluetoothAvailableCallback callback) {
  // Return true if there is an active adapter.
  auto self = weak_ptr_factory_.GetWeakPtr();
  bluetooth_manager_->GetKnownAdapters(
      [self, callback](const auto& adapter_map) {
        if (self) {
          callback(!adapter_map.empty());
        }
      });
}

void ControlServer::SetDelegate(
    ::fidl::InterfaceHandle<ControlDelegate> delegate) {
  if (!delegate) {
    FXL_VLOG(1) << "bluetooth: Cannot assign a null delegate";
    return;
  }

  delegate_ = delegate.Bind();
  delegate_.set_error_handler([this] {
    FXL_VLOG(1) << "bluetooth: ControlDelegate disconnected";
    delegate_ = nullptr;
  });
}

void ControlServer::SetPairingDelegate(
    bluetooth_control::InputCapabilityType in,
    bluetooth_control::OutputCapabilityType out,
    fidl::InterfaceHandle<PairingDelegate> delegate) {
  pairing_delegate_ = nullptr;
  if (!delegate) {
    return;
  }

  pairing_delegate_ = delegate.Bind();
  pairing_delegate_.set_error_handler([this] {
    FXL_VLOG(1) << "bluetooth: PairingDelegate disconnected";
    pairing_delegate_ = nullptr;
  });
}

void ControlServer::GetAdapters(GetAdaptersCallback callback) {
  auto self = weak_ptr_factory_.GetWeakPtr();
  bluetooth_manager_->GetKnownAdapters(
      [self, callback](const auto& adapter_map) {
        if (!self)
          return;

        fidl::VectorPtr<bluetooth_control::AdapterInfo> adapters;
        for (const auto& iter : adapter_map) {
          bluetooth_control::AdapterInfo info;
          iter.second.Clone(&info);
          adapters.push_back(std::move(info));
        }

        callback(std::move(adapters));
      });
}

void ControlServer::SetActiveAdapter(fidl::StringPtr identifier,
                                     SetActiveAdapterCallback callback) {
  bluetooth::Status status;
  auto ac =
      fxl::MakeAutoCall([&status, &callback] { callback(std::move(status)); });

  if (!bluetooth_manager_->SetActiveAdapter(identifier)) {
    status.error = Error::New();
    status.error->error_code = ErrorCode::NOT_FOUND;
    status.error->description = "Adapter not found";
  }
}

void ControlServer::GetActiveAdapterInfo(
    GetActiveAdapterInfoCallback callback) {
  auto self = weak_ptr_factory_.GetWeakPtr();
  bluetooth_manager_->GetActiveAdapter(fxl::MakeCopyable(
      [self, callback = std::move(callback)](auto* adapter) mutable {
        if (!self) {
          FXL_VLOG(2) << "bluetooth: Control disconnected before active "
                         "adapter was obtained";
          return;
        }

        if (!adapter) {
          FXL_VLOG(1) << "bluetooth: no active adapter";
          callback(nullptr);
          return;
        }

        auto info = bluetooth_control::AdapterInfo::New();
        adapter->info().Clone(info.get());
        callback(std::move(info));
      }));
}

void ControlServer::SetRemoteDeviceDelegate(
    fidl::InterfaceHandle<::bluetooth_control::RemoteDeviceDelegate> delegate,
    bool include_rssi) {
  device_delegate_ = nullptr;
  if (!delegate) {
    return;
  }

  device_delegate_ = delegate.Bind();
  device_delegate_.set_error_handler([this] {
    FXL_VLOG(1) << "bluetooth: RemoteDeviceDelegate disconnected";
  });
}

void ControlServer::RequestDiscovery(bool discovering,
                                     RequestDiscoveryCallback callback) {
  if (discovering && discovery_token_) {
    callback(Status());
    return;
  }

  if (!discovering) {
    discovery_token_ = nullptr;
    callback(Status());
    return;
  }

  auto self = weak_ptr_factory_.GetWeakPtr();
  bluetooth_manager_->RequestDiscovery(
      [self, callback = std::move(callback)](auto token, auto reason) {
        if (!self) {
          return;
        }
        if (!token) {
          bluetooth::Status status;
          status.error = Error::New();
          status.error->error_code = ErrorCode::FAILED;
          status.error->description = reason;
          callback(std::move(status));
          return;
        }

        self->discovery_token_ = std::move(token);
        callback(Status());
      });
}

void ControlServer::GetKnownRemoteDevices(
    GetKnownRemoteDevicesCallback callback) {
  FXL_NOTIMPLEMENTED();
}

void ControlServer::SetName(fidl::StringPtr name, SetNameCallback callback) {
  FXL_NOTIMPLEMENTED();
}

void ControlServer::SetDiscoverable(bool discoverable,
                                    SetDiscoverableCallback callback) {
  FXL_NOTIMPLEMENTED();
}

void ControlServer::Connect(fidl::StringPtr identifier,
                            bool permanent,
                            ConnectCallback callback) {
  FXL_NOTIMPLEMENTED();
}

void ControlServer::Disconnect(fidl::StringPtr identifier,
                               DisconnectCallback callback) {
  FXL_NOTIMPLEMENTED();
}

void ControlServer::Forget(fidl::StringPtr identifier,
                           ForgetCallback callback) {
  FXL_NOTIMPLEMENTED();
}

}  // namespace bluetooth_service
