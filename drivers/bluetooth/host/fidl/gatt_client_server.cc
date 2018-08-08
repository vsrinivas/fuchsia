// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gatt_client_server.h"

#include "garnet/drivers/bluetooth/lib/gatt/gatt.h"

#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/functional/make_copyable.h"

#include "gatt_remote_service_server.h"
#include "helpers.h"

using fuchsia::bluetooth::ErrorCode;
using fuchsia::bluetooth::Status;

using fuchsia::bluetooth::gatt::Client;
using fuchsia::bluetooth::gatt::RemoteService;
using fuchsia::bluetooth::gatt::ServiceInfo;
using fuchsia::bluetooth::gatt::ServiceInfoPtr;

namespace bthost {

GattClientServer::GattClientServer(std::string peer_id,
                                   fbl::RefPtr<btlib::gatt::GATT> gatt,
                                   fidl::InterfaceRequest<Client> request)
    : GattServerBase(gatt, this, std::move(request)),
      peer_id_(std::move(peer_id)),
      weak_ptr_factory_(this) {}

void GattClientServer::ListServices(
    ::fidl::VectorPtr<::fidl::StringPtr> fidl_uuids,
    ListServicesCallback callback) {
  // Parse the UUID list.
  std::vector<btlib::common::UUID> uuids;
  if (!fidl_uuids.is_null()) {
    // Allocate all at once and convert in-place.
    uuids.resize(fidl_uuids->size());
    for (size_t i = 0; i < uuids.size(); ++i) {
      if (!StringToUuid(fidl_uuids.get()[i].get(), &uuids[i])) {
        callback(fidl_helpers::NewFidlError(
                     ErrorCode::INVALID_ARGUMENTS,
                     "Invalid UUID: " + fidl_uuids.get()[i].get()),
                 fidl::VectorPtr<ServiceInfo>((size_t)0u));
        return;
      }
    }
  }

  auto cb = [callback = std::move(callback)](btlib::att::Status status,
                                             auto services) {
    std::vector<ServiceInfo> out;
    if (!status) {
      auto fidl_status =
          fidl_helpers::StatusToFidl(status, "Failed to discover services");
      callback(std::move(fidl_status),
               fidl::VectorPtr<ServiceInfo>(std::move(out)));
      return;
    }

    out.resize(services.size());

    size_t i = 0;
    for (const auto& svc : services) {
      ServiceInfo service_info;
      service_info.id = svc->handle();
      service_info.primary = true;
      service_info.type = svc->uuid().ToString();
      out[i++] = std::move(service_info);
    }
    callback(Status(), fidl::VectorPtr<ServiceInfo>(std::move(out)));
  };

  gatt()->ListServices(peer_id_, std::move(uuids), std::move(cb));
}

void GattClientServer::ConnectToService(
    uint64_t id, ::fidl::InterfaceRequest<RemoteService> service) {
  if (connected_services_.count(id)) {
    FXL_VLOG(1) << "GattClientServer: service already requested";
    return;
  }

  // Initialize an entry so that we remember when this request is in progress.
  connected_services_[id] = nullptr;

  auto self = weak_ptr_factory_.GetWeakPtr();
  auto callback = [self, id,
                   request = std::move(service)](auto service) mutable {
    if (!self)
      return;

    // The operation must be in progress.
    FXL_DCHECK(self->connected_services_.count(id));

    // Automatically called on failure.
    auto fail_cleanup =
        fxl::MakeAutoCall([self, id] { self->connected_services_.erase(id); });

    if (!service) {
      FXL_VLOG(1) << "GattClientServer: Failed to connect to service";
      return;
    }

    // Clean up the server if either the peer device or the FIDL client
    // disconnects.
    auto error_cb = [self, id] {
      FXL_VLOG(1) << "GattClientServer: service disconnected";
      if (self) {
        self->connected_services_.erase(id);
      }
    };

    if (!service->AddRemovedHandler(error_cb)) {
      FXL_VLOG(1) << "GattClientServer: failed to assign closed handler";
      return;
    }

    fail_cleanup.cancel();

    auto server = std::make_unique<GattRemoteServiceServer>(
        std::move(service), self->gatt(), std::move(request));
    server->set_error_handler(std::move(error_cb));

    self->connected_services_[id] = std::move(server);
  };

  gatt()->FindService(peer_id_, id, std::move(callback));
}

}  // namespace bthost
