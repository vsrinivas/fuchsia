// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/cpp/bluetooth_gatt.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

#include "garnet/drivers/bluetooth/host/fidl/server_base.h"
#include "garnet/drivers/bluetooth/lib/gatt/gatt.h"

namespace bthost {

// Implements the gatt::RemoteService FIDL interface.
class GattRemoteServiceServer
    : public GattServerBase<bluetooth_gatt::RemoteService> {
 public:
  GattRemoteServiceServer(
      fbl::RefPtr<btlib::gatt::RemoteService> service,
      fbl::RefPtr<btlib::gatt::GATT> gatt,
      fidl::InterfaceRequest<bluetooth_gatt::RemoteService> request);
  ~GattRemoteServiceServer() override = default;

 private:
  // bluetooth_gatt::RemoteService overrides:
  void SetDelegate(fidl::InterfaceHandle<bluetooth_gatt::RemoteServiceDelegate>
                       delegate) override {}
  void DiscoverCharacteristics(
      DiscoverCharacteristicsCallback callback) override;
  void WriteCharacteristic(uint64_t characteristic_id,
                           uint16_t offset,
                           ::fidl::VectorPtr<uint8_t> value,
                           WriteCharacteristicCallback callback) override;

  // The remote GATT service that backs this service.
  fbl::RefPtr<btlib::gatt::RemoteService> service_;

  fxl::WeakPtrFactory<GattRemoteServiceServer> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GattRemoteServiceServer);
};

}  // namespace bthost
