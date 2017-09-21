// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "adapter_fidl_impl.h"

#include "garnet/drivers/bluetooth/lib/gap/adapter.h"
#include "garnet/drivers/bluetooth/lib/gap/gap.h"
#include "garnet/drivers/bluetooth/lib/gap/low_energy_discovery_manager.h"
#include "lib/fxl/logging.h"

#include "fidl_helpers.h"

// The internal library components and the generated FIDL bindings are both declared under the
// "bluetooth" namespace. We define an alias here to disambiguate.
namespace btfidl = ::bluetooth;

namespace bluetooth_service {

AdapterFidlImpl::AdapterFidlImpl(const fxl::WeakPtr<::bluetooth::gap::Adapter>& adapter,
                                 ::fidl::InterfaceRequest<::btfidl::control::Adapter> request,
                                 const ConnectionErrorHandler& connection_error_handler)
    : adapter_(adapter),
      requesting_discovery_(false),
      binding_(this, std::move(request)),
      weak_ptr_factory_(this) {
  FXL_DCHECK(adapter_);
  FXL_DCHECK(connection_error_handler);
  binding_.set_connection_error_handler(
      [this, connection_error_handler] { connection_error_handler(this); });
}

void AdapterFidlImpl::GetInfo(const GetInfoCallback& callback) {
  callback(fidl_helpers::NewAdapterInfo(*adapter_));
}

void AdapterFidlImpl::SetDelegate(
    ::fidl::InterfaceHandle<::btfidl::control::AdapterDelegate> delegate) {
  if (delegate) {
    delegate_ = ::btfidl::control::AdapterDelegatePtr::Create(std::move(delegate));
  } else {
    delegate_ = nullptr;
  }
  if (delegate_) {
    delegate_.set_connection_error_handler([this] {
      FXL_LOG(INFO) << "Adapter delegate disconnected";
      delegate_ = nullptr;

      // TODO(armansito): Define a function for terminating all procedures that rely on a delegate.
      // For now we only support discovery, so we end it directly.
      if (le_discovery_session_) {
        le_discovery_session_ = nullptr;
        NotifyDiscoveringChanged();
      }
    });
  }

  // Setting a new delegate will terminate all on-going procedures associated with this
  // AdapterFidlImpl.
  if (le_discovery_session_) {
    le_discovery_session_ = nullptr;
    NotifyDiscoveringChanged();
  }
}

void AdapterFidlImpl::SetLocalName(const ::fidl::String& local_name,
                                   const ::fidl::String& shortened_local_name,
                                   const SetLocalNameCallback& callback) {
  FXL_NOTIMPLEMENTED();
}

void AdapterFidlImpl::SetPowered(bool powered, const SetPoweredCallback& callback) {
  FXL_NOTIMPLEMENTED();
}

void AdapterFidlImpl::StartDiscovery(const StartDiscoveryCallback& callback) {
  FXL_LOG(INFO) << "Adapter StartDiscovery()";

  if (!adapter_) {
    FXL_LOG(WARNING) << "Adapter not available";
    callback(fidl_helpers::NewErrorStatus(::btfidl::ErrorCode::NOT_FOUND, "Adapter not available"));
    return;
  }

  if (le_discovery_session_ || requesting_discovery_) {
    FXL_LOG(WARNING) << "Discovery already in progress";
    callback(fidl_helpers::NewErrorStatus(::btfidl::ErrorCode::IN_PROGRESS,
                                          "Discovery already in progress"));
    return;
  }

  requesting_discovery_ = true;
  auto manager = adapter_->le_discovery_manager();
  manager->StartDiscovery([ self = weak_ptr_factory_.GetWeakPtr(), callback ](auto session) {
    // End the new session if this AdapterFidlImpl got destroyed in the mean time (e.g. because the
    // client disconnected).
    if (!self) return;

    self->requesting_discovery_ = false;

    if (!session) {
      FXL_LOG(ERROR) << "Failed to start discovery session";
      callback(fidl_helpers::NewErrorStatus(::btfidl::ErrorCode::FAILED,
                                            "Failed to start discovery session"));
      return;
    }

    // Set up a general-discovery filter for connectable devices.
    session->filter()->set_connectable(true);
    session->SetResultCallback([self](const auto& device) {
      if (self) self->OnDiscoveryResult(device);
    });

    self->le_discovery_session_ = std::move(session);
    self->NotifyDiscoveringChanged();
    callback(::btfidl::Status::New());
  });
}

void AdapterFidlImpl::StopDiscovery(const StopDiscoveryCallback& callback) {
  FXL_LOG(INFO) << "Adapter StopDiscovery()";
  if (!le_discovery_session_) {
    FXL_LOG(ERROR) << "No active discovery session";
    callback(fidl_helpers::NewErrorStatus(::btfidl::ErrorCode::BAD_STATE,
                                          "No discovery session in progress"));
    return;
  }

  le_discovery_session_ = nullptr;
  NotifyDiscoveringChanged();
  callback(::btfidl::Status::New());
}

void AdapterFidlImpl::OnDiscoveryResult(const ::bluetooth::gap::RemoteDevice& remote_device) {
  if (!delegate_) return;

  auto fidl_device = fidl_helpers::NewRemoteDevice(remote_device);
  if (!fidl_device) {
    FXL_LOG(WARNING) << "Ignoring malformed discovery result";
    return;
  }

  delegate_->OnDeviceDiscovered(std::move(fidl_device));
}

void AdapterFidlImpl::NotifyDiscoveringChanged() {
  if (!delegate_) return;

  auto adapter_state = ::btfidl::control::AdapterState::New();
  adapter_state->discovering = ::btfidl::Bool::New();
  adapter_state->discovering->value = le_discovery_session_ != nullptr;

  delegate_->OnAdapterStateChanged(std::move(adapter_state));
}

}  // namespace bluetooth_service
