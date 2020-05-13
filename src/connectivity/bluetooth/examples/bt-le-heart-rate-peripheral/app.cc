// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <functional>
#include <iostream>

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
  peripheral_.events().OnPeerConnected = fit::bind_member(this, &App::OnPeerConnected);

  adv_handle_.set_error_handler([](zx_status_t s) {
    std::cout << "LE advertising was stopped: " << zx_status_get_string(s) << std::endl;
  });
  connection_.set_error_handler([](zx_status_t s) {
    std::cout << "connection to peer dropped: " << zx_status_get_string(s) << std::endl;
  });
}

void App::StartAdvertising() {
  fuchsia::bluetooth::Uuid uuid{{0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00,
                                 0x00, 0x0d, 0x18, 0x00, 0x00}};
  ble::AdvertisingData ad;
  ad.set_name(kDeviceName);
  ad.set_service_uuids({{uuid}});

  ble::AdvertisingParameters params;
  params.set_connectable(true);
  params.set_data(std::move(ad));
  params.set_mode_hint(ble::AdvertisingModeHint::FAST);

  peripheral_->StartAdvertising(std::move(params), adv_handle_.NewRequest(), [](auto result) {
    if (result.is_err()) {
      std::cout << "StartAdvertising failed: ";
      switch (result.err()) {
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
    } else {
      std::cout << "started advertising" << std::endl;
    }
  });
}

void App::OnPeerConnected(fuchsia::bluetooth::le::Peer peer,
                          fidl::InterfaceHandle<fuchsia::bluetooth::le::Connection> handle) {
  std::cout << "received connection from peer (id: " << peer.id().value << ")" << std::endl;
  connection_.Bind(std::move(handle));
}

}  // namespace bt_le_heart_rate
