// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <functional>
#include <iostream>

#include <src/lib/fxl/strings/string_printf.h>

#include "heart_model.h"

namespace ble = fuchsia::bluetooth::le;

namespace bt_le_heart_rate {

App::App(std::unique_ptr<HeartModel> heart_model)
    : context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()),
      service_(std::move(heart_model)) {
  gatt_server_ = context_->svc()->Connect<fuchsia::bluetooth::gatt::Server>();
  FX_DCHECK(gatt_server_);

  service_.PublishService(&gatt_server_);

  peripheral_ = context_->svc()->Connect<ble::Peripheral>();
  FX_DCHECK(peripheral_);
}

void App::StartAdvertising() {
  fuchsia::bluetooth::Uuid uuid{{0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00,
                                 0x00, 0x0d, 0x18, 0x00, 0x00}};
  ble::AdvertisingData ad;
  ad.set_name(kDeviceName);
  ad.set_service_uuids({{uuid}});

  ble::AdvertisingParameters params;
  params.set_connection_options(ble::ConnectionOptions());
  params.set_data(std::move(ad));
  params.set_mode_hint(ble::AdvertisingModeHint::FAST);

  fidl::InterfaceHandle<ble::AdvertisedPeripheral> handle;
  advertised_peripheral_.emplace(this, handle.NewRequest());
  peripheral_->Advertise(std::move(params), std::move(handle), [](auto stopped_result) {
    if (stopped_result.is_err()) {
      std::cout << "Advertise error: ";
      switch (stopped_result.err()) {
        case ble::PeripheralError::NOT_SUPPORTED:
          std::cout << "not supported";
          break;
        case ble::PeripheralError::ADVERTISING_DATA_TOO_LONG:
          std::cout << "advertising data too long";
          break;
        case ble::PeripheralError::SCAN_RESPONSE_DATA_TOO_LONG:
          std::cout << "scan response data too long";
          break;
        case ble::PeripheralError::INVALID_PARAMETERS:
          std::cout << "invalid parameters";
          break;
        case ble::PeripheralError::FAILED:
        default:
          std::cout << "failed";
          break;
      }
      std::cout << std::endl;
    }
  });

  std::cout << "started advertising" << std::endl;
}

App::AdvertisedPeripheral::AdvertisedPeripheral(
    App* app, fidl::InterfaceRequest<ble::AdvertisedPeripheral> request)
    : app_(app), binding_(this, std::move(request)) {
  binding_.set_error_handler([this](zx_status_t status) {
    std::cout << "advertising stopped with status: " << zx_status_get_string(status) << std::endl;
    app_->advertised_peripheral_.reset();
  });
}

void App::AdvertisedPeripheral::OnConnected(
    fuchsia::bluetooth::le::Peer peer,
    fidl::InterfaceHandle<fuchsia::bluetooth::le::Connection> connection,
    OnConnectedCallback callback) {
  std::string peer_id = fxl::StringPrintf("%.16lx", peer.id().value);
  std::cout << "received connection (peer: " << peer_id << ")" << std::endl;

  app_->connection_.Bind(std::move(connection));

  app_->connection_.set_error_handler([peer_id](zx_status_t status) {
    std::cout << "connection to peer " << peer_id
              << " closed with status: " << zx_status_get_string(status) << std::endl;
  });

  callback();
}

}  // namespace bt_le_heart_rate
