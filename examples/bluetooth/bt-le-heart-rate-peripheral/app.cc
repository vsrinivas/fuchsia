// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app.h"

#include <functional>
#include <iostream>

#include <lib/fxl/logging.h>

namespace bt_le_heart_rate {

App::App(std::unique_ptr<HeartModel> heart_model)
    : context_(component::ApplicationContext::CreateFromStartupInfo()),
      service_(std::move(heart_model)) {
  gatt_server_ =
      context_->ConnectToEnvironmentService<bluetooth_gatt::Server>();
  FXL_DCHECK(gatt_server_);

  service_.PublishService(&gatt_server_);

  peripheral_ =
      context_->ConnectToEnvironmentService<bluetooth_low_energy::Peripheral>();
  FXL_DCHECK(peripheral_);
}

void App::StartAdvertising() {
  bluetooth_low_energy::AdvertisingData ad;
  ad.name = kDeviceName;
  ad.service_uuids = fidl::VectorPtr<fidl::StringPtr>({Service::kServiceUuid});

  const auto start_adv_result_cb = [](bluetooth::Status status,
                                      fidl::StringPtr advertisement_id) {
    std::cout << "StartAdvertising status: " << bool(status.error)
              << ", advertisement_id: " << advertisement_id << std::endl;
  };

  binding_ = std::make_unique<Binding>(this);
  peripheral_->StartAdvertising(std::move(ad), nullptr, binding_->NewBinding(),
                                60, false, std::move(start_adv_result_cb));
}

constexpr char App::kDeviceName[];

void App::OnCentralConnected(fidl::StringPtr advertisement_id,
                             bluetooth_low_energy::RemoteDevice central) {
  std::cout << "Central (" << central.identifier << ") connected" << std::endl;

  // Save the binding for this connection.
  connected_bindings_.emplace(*central.identifier, std::move(binding_));

  // Start another advertisement so other peers can connect.
  StartAdvertising();
}

void App::OnCentralDisconnected(fidl::StringPtr device_id) {
  std::cout << "Central (" << device_id << ") disconnected" << std::endl;

  connected_bindings_.erase(device_id);
}

}  // namespace bt_le_heart_rate
