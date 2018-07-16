// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app.h"

#include <functional>
#include <iostream>

#include <lib/fxl/logging.h>

namespace ble = fuchsia::bluetooth::le;

namespace bt_le_heart_rate {

App::App(std::unique_ptr<HeartModel> heart_model)
    : context_(component::StartupContext::CreateFromStartupInfo()),
      service_(std::move(heart_model)) {
  gatt_server_ =
      context_->ConnectToEnvironmentService<fuchsia::bluetooth::gatt::Server>();
  FXL_DCHECK(gatt_server_);

  service_.PublishService(&gatt_server_);

  peripheral_ = context_->ConnectToEnvironmentService<ble::Peripheral>();
  FXL_DCHECK(peripheral_);
}

void App::StartAdvertising() {
  ble::AdvertisingData ad;
  ad.name = kDeviceName;
  ad.service_uuids = fidl::VectorPtr<fidl::StringPtr>({Service::kServiceUuid});

  const auto start_adv_result_cb = [](fuchsia::bluetooth::Status status,
                                      fidl::StringPtr advertisement_id) {
    std::cout << "StartAdvertising status: " << bool(status.error)
              << ", advertisement_id: " << advertisement_id << std::endl;
  };

  peripheral_.events().OnCentralConnected =
      fit::bind_member(this, &App::OnCentralConnected);
  peripheral_.events().OnCentralDisconnected =
      fit::bind_member(this, &App::OnCentralDisconnected);
  peripheral_->StartAdvertising(std::move(ad), nullptr, 60, false,
                                std::move(start_adv_result_cb));
}

constexpr char App::kDeviceName[];

void App::OnCentralConnected(fidl::StringPtr advertisement_id,
                             ble::RemoteDevice central) {
  std::cout << "Central (" << central.identifier << ") connected" << std::endl;

  // Start another advertisement so other peers can connect.
  StartAdvertising();
}

void App::OnCentralDisconnected(fidl::StringPtr device_id) {
  std::cout << "Central (" << device_id << ") disconnected" << std::endl;
}

}  // namespace bt_le_heart_rate
