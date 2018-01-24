// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "adapter_server.h"

#include "garnet/drivers/bluetooth/lib/gap/adapter.h"
#include "garnet/drivers/bluetooth/lib/gap/gap.h"
#include "garnet/drivers/bluetooth/lib/gap/low_energy_discovery_manager.h"
#include "lib/fxl/logging.h"

#include "helpers.h"

using bluetooth::Bool;
using bluetooth::ErrorCode;
using bluetooth::Status;

using bluetooth::control::AdapterDelegate;
using bluetooth::control::AdapterDelegatePtr;
using bluetooth::control::AdapterState;

namespace bthost {

AdapterServer::AdapterServer(
    fxl::WeakPtr<::btlib::gap::Adapter> adapter,
    fidl::InterfaceRequest<bluetooth::control::Adapter> request)
    : ServerBase(adapter, this, std::move(request)),
      requesting_discovery_(false),
      weak_ptr_factory_(this) {}

AdapterServer::~AdapterServer() {}

void AdapterServer::GetInfo(const GetInfoCallback& callback) {
  callback(fidl_helpers::NewAdapterInfo(*adapter()));
}

void AdapterServer::SetDelegate(
    fidl::InterfaceHandle<AdapterDelegate> delegate) {
  if (delegate) {
    delegate_ = delegate.Bind();
  } else {
    delegate_ = nullptr;
  }
  if (delegate_) {
    delegate_.set_error_handler([this] {
      FXL_VLOG(1) << "Adapter delegate disconnected";
      delegate_ = nullptr;

      // TODO(armansito): Define a function for terminating all procedures that
      // rely on a delegate. For now we only support discovery, so we end it
      // directly.
      if (le_discovery_session_) {
        le_discovery_session_ = nullptr;
        NotifyDiscoveringChanged();
      }
    });
  }

  // Setting a new delegate will terminate all on-going procedures associated
  // with this AdapterServer.
  if (le_discovery_session_) {
    le_discovery_session_ = nullptr;
    NotifyDiscoveringChanged();
  }
}

void AdapterServer::SetLocalName(const fidl::String& local_name,
                                 const fidl::String& shortened_local_name,
                                 const SetLocalNameCallback& callback) {
  FXL_NOTIMPLEMENTED();
}

void AdapterServer::SetPowered(bool powered,
                               const SetPoweredCallback& callback) {
  FXL_NOTIMPLEMENTED();
}

void AdapterServer::StartDiscovery(const StartDiscoveryCallback& callback) {
  FXL_VLOG(1) << "Adapter StartDiscovery()";
  FXL_DCHECK(adapter());

  if (le_discovery_session_ || requesting_discovery_) {
    FXL_VLOG(1) << "Discovery already in progress";
    callback(fidl_helpers::NewErrorStatus(ErrorCode::IN_PROGRESS,
                                          "Discovery already in progress"));
    return;
  }

  requesting_discovery_ = true;
  auto manager = adapter()->le_discovery_manager();
  manager->StartDiscovery(
      [self = weak_ptr_factory_.GetWeakPtr(), callback](auto session) {
        // End the new session if this AdapterServer got destroyed in the mean
        // time (e.g. because the client disconnected).
        if (!self)
          return;

        self->requesting_discovery_ = false;

        if (!session) {
          FXL_VLOG(1) << "Failed to start discovery session";
          callback(fidl_helpers::NewErrorStatus(
              ErrorCode::FAILED, "Failed to start discovery session"));
          return;
        }

        // Set up a general-discovery filter for connectable devices.
        session->filter()->set_connectable(true);
        session->SetResultCallback([self](const auto& device) {
          if (self)
            self->OnDiscoveryResult(device);
        });

        self->le_discovery_session_ = std::move(session);
        self->NotifyDiscoveringChanged();
        callback(Status::New());
      });
}

void AdapterServer::StopDiscovery(const StopDiscoveryCallback& callback) {
  FXL_VLOG(1) << "Adapter StopDiscovery()";
  if (!le_discovery_session_) {
    FXL_VLOG(1) << "No active discovery session";
    callback(fidl_helpers::NewErrorStatus(ErrorCode::BAD_STATE,
                                          "No discovery session in progress"));
    return;
  }

  le_discovery_session_ = nullptr;
  NotifyDiscoveringChanged();
  callback(Status::New());
}

void AdapterServer::OnDiscoveryResult(
    const ::btlib::gap::RemoteDevice& remote_device) {
  if (!delegate_)
    return;

  auto fidl_device = fidl_helpers::NewRemoteDevice(remote_device);
  if (!fidl_device) {
    FXL_VLOG(1) << "Ignoring malformed discovery result";
    return;
  }

  delegate_->OnDeviceDiscovered(std::move(fidl_device));
}

void AdapterServer::NotifyDiscoveringChanged() {
  if (!delegate_)
    return;

  auto adapter_state = AdapterState::New();
  adapter_state->discovering = Bool::New();
  adapter_state->discovering->value = le_discovery_session_ != nullptr;

  delegate_->OnAdapterStateChanged(std::move(adapter_state));
}

}  // namespace bthost
