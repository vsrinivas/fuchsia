// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gatt_client_server.h"

#include "garnet/drivers/bluetooth/lib/gatt/gatt.h"

#include "helpers.h"

using bluetooth::ErrorCode;
using bluetooth::Status;

using bluetooth_gatt::Client;
using bluetooth_gatt::RemoteService;
using bluetooth_gatt::ServiceInfo;
using bluetooth_gatt::ServiceInfoPtr;

namespace bthost {

GattClientServer::GattClientServer(std::string peer_id,
                                   fbl::RefPtr<btlib::gatt::GATT> gatt,
                                   fidl::InterfaceRequest<Client> request)
    : GattServerBase(gatt, this, std::move(request)),
      peer_id_(std::move(peer_id)) {}

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
                 nullptr);
        return;
      }
    }
  }

  auto cb = [callback = std::move(callback)](btlib::att::Status status,
                                             auto services) {
    if (!status) {
      auto fidl_status =
          fidl_helpers::StatusToFidl(status, "Failed to discover services");
      callback(std::move(fidl_status), nullptr);
      return;
    }

    std::vector<ServiceInfo> out(services.size());

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
    uint64_t id,
    ::fidl::InterfaceRequest<RemoteService> service) {
  FXL_NOTIMPLEMENTED();
}

}  // namespace bthost
