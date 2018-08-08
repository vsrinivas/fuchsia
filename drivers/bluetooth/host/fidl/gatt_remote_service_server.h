// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include <fuchsia/bluetooth/gatt/cpp/fidl.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

#include "garnet/drivers/bluetooth/host/fidl/server_base.h"
#include "garnet/drivers/bluetooth/lib/gatt/gatt.h"

namespace bthost {

// Implements the gatt::RemoteService FIDL interface.
class GattRemoteServiceServer
    : public GattServerBase<fuchsia::bluetooth::gatt::RemoteService> {
 public:
  GattRemoteServiceServer(
      fbl::RefPtr<btlib::gatt::RemoteService> service,
      fbl::RefPtr<btlib::gatt::GATT> gatt,
      fidl::InterfaceRequest<fuchsia::bluetooth::gatt::RemoteService> request);
  ~GattRemoteServiceServer() override;

 private:
  // fuchsia::bluetooth::gatt::RemoteService overrides:
  void DiscoverCharacteristics(
      DiscoverCharacteristicsCallback callback) override;
  void ReadCharacteristic(uint64_t id,
                          ReadCharacteristicCallback callback) override;
  void ReadLongCharacteristic(uint64_t id, uint16_t offset, uint16_t max_bytes,
                              ReadLongCharacteristicCallback callback) override;
  void WriteCharacteristic(uint64_t characteristic_id, uint16_t offset,
                           ::fidl::VectorPtr<uint8_t> value,
                           WriteCharacteristicCallback callback) override;
  void WriteCharacteristicWithoutResponse(
      uint64_t id, ::fidl::VectorPtr<uint8_t> value) override;
  void NotifyCharacteristic(uint64_t id, bool enable,
                            NotifyCharacteristicCallback callback) override;

  // The remote GATT service that backs this service.
  fbl::RefPtr<btlib::gatt::RemoteService> service_;

  // Maps characteristic IDs to notification handler IDs.
  std::unordered_map<btlib::gatt::IdType, btlib::gatt::IdType> notify_handlers_;

  fxl::WeakPtrFactory<GattRemoteServiceServer> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GattRemoteServiceServer);
};

}  // namespace bthost
