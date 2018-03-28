// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gatt_remote_service_server.h"

#include "helpers.h"

using bluetooth::Status;

using bluetooth_gatt::Characteristic;
using bluetooth_gatt::CharacteristicPtr;
using btlib::gatt::RemoteCharacteristic;

namespace bthost {
namespace {

// We mask away the "extended properties" property. We expose extended
// properties in the same bitfield.
constexpr uint8_t kPropertyMask = 0x7F;

Characteristic CharacteristicToFidl(const RemoteCharacteristic& chrc) {
  Characteristic fidl;
  fidl.id = chrc.id();
  fidl.type = chrc.info().type.ToString();
  fidl.properties =
      static_cast<uint16_t>(chrc.info().properties & kPropertyMask);

  // TODO(armansito): Add extended properties.

  return fidl;
}

}  // namespace

GattRemoteServiceServer::GattRemoteServiceServer(
    fbl::RefPtr<btlib::gatt::RemoteService> service,
    fbl::RefPtr<btlib::gatt::GATT> gatt,
    fidl::InterfaceRequest<bluetooth_gatt::RemoteService> request)
    : GattServerBase(gatt, this, std::move(request)),
      service_(std::move(service)),
      weak_ptr_factory_(this) {
  FXL_DCHECK(service_);
}

void GattRemoteServiceServer::DiscoverCharacteristics(
    DiscoverCharacteristicsCallback callback) {
  auto res_cb = [callback = std::move(callback)](btlib::att::Status status,
                                                 const auto& chrcs) {
    std::vector<Characteristic> fidl_chrcs;
    if (status) {
      for (auto& chrc : chrcs) {
        fidl_chrcs.push_back(CharacteristicToFidl(chrc));
      }
    }

    callback(fidl_helpers::StatusToFidl(status, ""),
             fidl::VectorPtr<Characteristic>(std::move(fidl_chrcs)));
  };

  service_->DiscoverCharacteristics(std::move(res_cb));
}

void GattRemoteServiceServer::WriteCharacteristic(
    uint64_t id,
    uint16_t offset,
    ::fidl::VectorPtr<uint8_t> value,
    WriteCharacteristicCallback callback) {
  auto res_cb = [callback](btlib::att::Status status) {
    callback(fidl_helpers::StatusToFidl(status, ""));
  };

  // TODO(armansito): Use |offset| when gatt::RemoteService supports the long
  // write procedure.
  service_->WriteCharacteristic(id, value.take(), std::move(res_cb));
}

}  // namespace bthost
