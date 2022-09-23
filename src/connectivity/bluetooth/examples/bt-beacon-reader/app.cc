// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app.h"

#include <fuchsia/bluetooth/le/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <time.h>

#include <iostream>

#include "beacons.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace ble = fuchsia::bluetooth::le;

namespace bt_beacon_reader {

namespace {

std::string UuidToString(const fuchsia::bluetooth::Uuid& uuid) {
  std::array<uint8_t, 16> value = uuid.value;
  return fxl::StringPrintf("%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                           value[15], value[14], value[13], value[12], value[11], value[10],
                           value[9], value[8], value[7], value[6], value[5], value[4], value[3],
                           value[2], value[1], value[0]);
}

}  // namespace

App::App(async::Loop* loop, bool just_tilts)
    : loop_(loop),
      context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()),
      just_tilts_(just_tilts) {
  FX_DCHECK(context_);

  central_ = context_->svc()->Connect<ble::Central>();
  FX_DCHECK(central_);

  central_.set_error_handler([this](zx_status_t status) {
    printf("Central disconnected\n");
    loop_->Quit();
  });
}

void App::StartScanning() {
  ble::ScanOptions options;
  ble::Filter filter;
  filter.set_connectable(false);
  options.mutable_filters()->push_back(std::move(filter));
  central_->Scan(std::move(options), result_watcher_.NewRequest(),
                 [] { printf("scanning stopped\n"); });
  Watch();
}

void App::Watch() {
  result_watcher_->Watch([this](std::vector<ble::Peer> peers) {
    for (ble::Peer& peer : peers) {
      OnPeerDiscovered(peer);
    }
    Watch();
  });
}

void PrintRDHeader(const ble::Peer& peer) {
  printf("id: %.16lx ", peer.id().value);
  if (peer.has_data() && peer.data().has_appearance()) {
    const uint16_t appearance = static_cast<uint16_t>(peer.data().appearance());
    printf("Appearance: %u  ", appearance);
  }
  if (peer.has_name()) {
    printf("Name: %s  ", peer.name().c_str());
  }
  printf("\n");
}

void PrintGeneralBeaconData(const ble::Peer& peer) {
  if (!peer.has_data()) {
    return;
  }

  if (peer.data().has_service_data()) {
    for (const ble::ServiceData& data : peer.data().service_data()) {
      printf("  S  uuid: %s   data: 0x", UuidToString(data.uuid).c_str());
      for (uint8_t byte : data.data) {
        printf("%02x", byte);
      }
      printf("\n");
    }
  }
  if (peer.data().has_manufacturer_data()) {
    for (const ble::ManufacturerData& data : peer.data().manufacturer_data()) {
      printf("  M  cid: 0x%04x   data: 0x", data.company_id);
      for (uint8_t byte : data.data) {
        printf("%02x", byte);
      }
      printf("\n");
    }
  }
}

void App::OnPeerDiscovered(const ble::Peer& peer) const {
  if (just_tilts_) {
    std::unique_ptr<TiltDetection> tilt = TiltDetection::Create(peer);
    if (tilt) {
      tilt->Print();
    }
  } else {
    PrintRDHeader(peer);
    PrintGeneralBeaconData(peer);
  }
}

}  // namespace bt_beacon_reader
