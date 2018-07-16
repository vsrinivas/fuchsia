// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app.h"
#include "beacons.h"

#include <time.h>
#include <iostream>

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include <fuchsia/bluetooth/control/cpp/fidl.h>
#include <fuchsia/bluetooth/le/cpp/fidl.h>
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/split_string.h"

namespace ble = fuchsia::bluetooth::le;

namespace bt_beacon_reader {

App::App(async::Loop* loop, bool just_tilts)
    : loop_(loop),
      context_(component::StartupContext::CreateFromStartupInfo()),
      central_delegate_(this),
      just_tilts_(just_tilts) {
  FXL_DCHECK(context_);

  central_ = context_->ConnectToEnvironmentService<ble::Central>();
  FXL_DCHECK(central_);

  central_.set_error_handler([this] {
    printf("Central disconnected\n");
    loop_->Quit();
  });

  // Register with the Control as its delegate.
  ble::CentralDelegatePtr delegate;
  central_delegate_.Bind(delegate.NewRequest());
  central_->SetDelegate(std::move(delegate));
}

void App::StartScanning() {
  ble::ScanFilterPtr filter = ble::ScanFilter::New();
  filter->connectable = fuchsia::bluetooth::Bool::New();
  filter->connectable->value = false;
  central_->StartScan(std::move(filter),
                      [](fuchsia::bluetooth::Status status) {});
}

// Called when the scan state changes, e.g. when a scan session terminates due
// to a call to Central.StopScan() or another unexpected condition.
void App::OnScanStateChanged(bool scanning) {
  printf("Device %s scanning.\n", scanning ? "started" : "stopped");
}

void PrintRDHeader(const ble::RemoteDevice& device) {
  printf("id: %s ", device.identifier->c_str());
  if (device.advertising_data && device.advertising_data->appearance) {
    uint16_t appearance = device.advertising_data->appearance->value;
    printf("Appearance: %u  ", appearance);
  }

  if (device.advertising_data && device.advertising_data->name) {
    printf("Name: %s  ", device.advertising_data->name->c_str());
  }
  printf("\n");
}

void PrintGeneralBeaconData(const ble::RemoteDevice& device) {
  if (!device.advertising_data) {
    return;
  }
  for (auto& data : *device.advertising_data->service_data) {
    printf("  S  uuid: %s   data: 0x", data.uuid->c_str());
    for (auto& byte : *data.data) {
      printf("%02x", byte);
    }
    printf("\n");
  }
  for (auto& data : *device.advertising_data->manufacturer_specific_data) {
    printf("  M  cid: 0x%04x   data: 0x", data.company_id);
    for (auto& byte : *data.data) {
      printf("%02x", byte);
    }
    printf("\n");
  }
}

void App::OnDeviceDiscovered(ble::RemoteDevice device) {
  if (just_tilts_) {
    std::unique_ptr<TiltDetection> tilt = TiltDetection::Create(device);
    if (tilt) {
      tilt->Print();
    }
  } else {
    PrintRDHeader(device);
    PrintGeneralBeaconData(device);
  }
}

// Called when this Central's connection to a peripheral with the given
// identifier is terminated.
void App::OnPeripheralDisconnected(::fidl::StringPtr identifier) {
  printf("Peripheral Disconnected: %s\n", identifier->c_str());
}
}  // namespace bt_beacon_reader
