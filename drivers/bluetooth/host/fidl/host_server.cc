// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "host_server.h"

#include "garnet/drivers/bluetooth/lib/gap/adapter.h"

#include "garnet/drivers/bluetooth/host/gatt_host.h"
#include "garnet/drivers/bluetooth/lib/gap/bredr_connection_manager.h"
#include "garnet/drivers/bluetooth/lib/gap/bredr_discovery_manager.h"
#include "garnet/drivers/bluetooth/lib/gap/gap.h"
#include "garnet/drivers/bluetooth/lib/gap/low_energy_discovery_manager.h"
#include "lib/fxl/logging.h"

#include "helpers.h"
#include "low_energy_central_server.h"
#include "low_energy_peripheral_server.h"

namespace bthost {

using fuchsia::bluetooth::Bool;
using fuchsia::bluetooth::ErrorCode;
using fuchsia::bluetooth::Status;
using fuchsia::bluetooth::control::AdapterState;

HostServer::HostServer(zx::channel channel,
                       fxl::WeakPtr<::btlib::gap::Adapter> adapter,
                       fbl::RefPtr<GattHost> gatt_host)
    : AdapterServerBase(adapter, this, std::move(channel)),
      gatt_host_(gatt_host),
      weak_ptr_factory_(this) {
  FXL_DCHECK(gatt_host_);
  adapter->remote_device_cache()->set_device_updated_callback(
      [self = weak_ptr_factory_.GetWeakPtr()](const auto& device) {
        if (self) {
          self->OnRemoteDeviceUpdated(device);
        }
      });
}

void HostServer::GetInfo(GetInfoCallback callback) {
  callback(fidl_helpers::NewAdapterInfo(*adapter()));
}

void HostServer::SetLocalName(::fidl::StringPtr local_name,
                              SetLocalNameCallback callback) {
  adapter()->SetLocalName(
      local_name, [self = weak_ptr_factory_.GetWeakPtr(),
                   callback = std::move(callback)](auto status) {
        callback(fidl_helpers::StatusToFidl(status, "Can't Set Local Name"));
      });
}

void HostServer::StartDiscovery(StartDiscoveryCallback callback) {
  FXL_VLOG(1) << "Adapter StartDiscovery()";
  FXL_DCHECK(adapter());

  if (le_discovery_session_ || requesting_discovery_) {
    FXL_VLOG(1) << "Discovery already in progress";
    callback(fidl_helpers::NewFidlError(ErrorCode::IN_PROGRESS,
                                        "Discovery already in progress"));
    return;
  }

  requesting_discovery_ = true;
  auto bredr_manager = adapter()->bredr_discovery_manager();
  // TODO(jamuraa): start these in parallel instead of sequence
  bredr_manager->RequestDiscovery(
      [self = weak_ptr_factory_.GetWeakPtr(), bredr_manager,
       callback = std::move(callback)](btlib::hci::Status status,
                                       auto session) mutable {
        if (!self) {
          callback(fidl_helpers::NewFidlError(ErrorCode::FAILED,
                                              "Adapter Shutdown"));
          return;
        }

        if (!status || !session) {
          FXL_VLOG(1) << "Failed to start BR/EDR discovery session";
          callback(fidl_helpers::StatusToFidl(
              status, "Failed to start BR/EDR discovery session"));
          self->requesting_discovery_ = false;
          return;
        }

        self->bredr_discovery_session_ = std::move(session);

        auto le_manager = self->adapter()->le_discovery_manager();
        le_manager->StartDiscovery(
            [self, callback = std::move(callback)](auto session) {
              // End the new session if this AdapterServer got destroyed in the
              // mean time (e.g. because the client disconnected).
              if (!self) {
                callback(fidl_helpers::NewFidlError(ErrorCode::FAILED,
                                                    "Adapter Shutdown"));
                return;
              }

              if (!session) {
                FXL_VLOG(1) << "Failed to start LE discovery session";
                callback(fidl_helpers::NewFidlError(
                    ErrorCode::FAILED, "Failed to start LE discovery session"));
                self->bredr_discovery_session_ = nullptr;
                self->requesting_discovery_ = false;
                return;
              }

              // Set up a general-discovery filter for connectable devices.
              session->filter()->set_connectable(true);
              session->filter()->SetGeneralDiscoveryFlags();

              self->le_discovery_session_ = std::move(session);
              self->requesting_discovery_ = false;

              // Send the adapter state update.
              AdapterState state;
              state.discovering = Bool::New();
              state.discovering->value = true;
              self->binding()->events().OnHostStateChanged(std::move(state));

              callback(Status());
            });
      });
}

void HostServer::StopDiscovery(StopDiscoveryCallback callback) {
  FXL_VLOG(1) << "Adapter StopDiscovery()";
  if (!le_discovery_session_) {
    FXL_VLOG(1) << "No active discovery session";
    callback(fidl_helpers::NewFidlError(ErrorCode::BAD_STATE,
                                        "No discovery session in progress"));
    return;
  }

  bredr_discovery_session_ = nullptr;
  le_discovery_session_ = nullptr;

  AdapterState state;
  state.discovering = Bool::New();
  state.discovering->value = false;
  this->binding()->events().OnHostStateChanged(std::move(state));

  callback(Status());
}

void HostServer::SetConnectable(bool connectable,
                                SetConnectableCallback callback) {
  FXL_VLOG(1) << "Adapter SetConnectable(" << connectable << ")";

  adapter()->bredr_connection_manager()->SetConnectable(
      connectable, [callback = std::move(callback)](const auto& status) {
        callback(fidl_helpers::StatusToFidl(status));
      });
}

void HostServer::SetDiscoverable(bool discoverable,
                                 SetDiscoverableCallback callback) {
  FXL_VLOG(1) << "Adapter SetDiscoverable(" << discoverable << ")";
  // TODO(NET-830): advertise LE here
  if (!discoverable) {
    bredr_discoverable_session_ = nullptr;

    AdapterState state;
    state.discoverable = Bool::New();
    state.discoverable->value = false;
    this->binding()->events().OnHostStateChanged(std::move(state));

    callback(Status());
    return;
  }
  if (discoverable && requesting_discoverable_) {
    FXL_VLOG(1) << "Discoverable already being set";
    callback(fidl_helpers::NewFidlError(ErrorCode::IN_PROGRESS,
                                        "SetDiscoverable already in progress"));
    return;
  }
  requesting_discoverable_ = true;
  auto bredr_manager = adapter()->bredr_discovery_manager();
  bredr_manager->RequestDiscoverable(
      [self = weak_ptr_factory_.GetWeakPtr(), callback = std::move(callback)](
          btlib::hci::Status status, auto session) {
        if (!self) {
          callback(fidl_helpers::NewFidlError(ErrorCode::FAILED,
                                              "Adapter Shutdown"));
          return;
        }
        if (!status || !session) {
          FXL_VLOG(1) << "Failed to set discoverable!";
          callback(
              fidl_helpers::StatusToFidl(status, "Failed to set discoverable"));
          self->requesting_discoverable_ = false;
        }
        self->bredr_discoverable_session_ = std::move(session);
        AdapterState state;
        state.discoverable = Bool::New();
        state.discoverable->value = true;
        self->binding()->events().OnHostStateChanged(std::move(state));
        callback(Status());
      });
}

void HostServer::RequestLowEnergyCentral(
    fidl::InterfaceRequest<fuchsia::bluetooth::le::Central> request) {
  BindServer<LowEnergyCentralServer>(std::move(request), gatt_host_);
}

void HostServer::RequestLowEnergyPeripheral(
    fidl::InterfaceRequest<fuchsia::bluetooth::le::Peripheral> request) {
  BindServer<LowEnergyPeripheralServer>(std::move(request));
}

void HostServer::RequestGattServer(
    fidl::InterfaceRequest<fuchsia::bluetooth::gatt::Server> request) {
  // GATT FIDL requests are handled by GattHost.
  gatt_host_->BindGattServer(std::move(request));
}

void HostServer::Close() {
  FXL_VLOG(1) << "bthost: Closing FIDL handles";

  // Destroy all bindings.
  servers_.clear();
  gatt_host_->CloseServers();
}

void HostServer::OnConnectionError(Server* server) {
  FXL_DCHECK(server);
  servers_.erase(server);
}

void HostServer::OnRemoteDeviceUpdated(
    const ::btlib::gap::RemoteDevice& remote_device) {
  auto fidl_device = fidl_helpers::NewRemoteDevice(remote_device);
  if (!fidl_device) {
    FXL_VLOG(1) << "Ignoring malformed device update";
    return;
  }

  this->binding()->events().OnDeviceUpdated(std::move(*fidl_device));
}

}  // namespace bthost
