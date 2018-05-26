// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app.h"
#include "beacons.h"

#include <time.h>
#include <iostream>

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include <bluetooth_control/cpp/fidl.h>
#include <bluetooth_low_energy/cpp/fidl.h>
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/split_string.h"

char* date_string() {
  static char date_buf[80];
  time_t t = time(NULL);
  struct tm tm = *localtime(&t);

  snprintf(date_buf, sizeof(date_buf), "%4d-%02d-%02d %02d:%02d:%02d",
           tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
           tm.tm_sec);
  return date_buf;
}

namespace bt_beacon_reader {

App::App(async::Loop* loop, bool just_tilts)
    : loop_(loop),
      context_(component::ApplicationContext::CreateFromStartupInfo()),
      central_delegate_(this),
      just_tilts_(just_tilts) {
  FXL_DCHECK(context_);

  central_ =
      context_->ConnectToEnvironmentService<bluetooth_low_energy::Central>();
  FXL_DCHECK(central_);

  central_.set_error_handler([this] {
    printf("Central disconnected\n");
    loop_->Quit();
  });

  // Register with the Control as its delegate.
  bluetooth_low_energy::CentralDelegatePtr delegate;
  central_delegate_.Bind(delegate.NewRequest());
  central_->SetDelegate(std::move(delegate));
}

void App::StartScanning() {
  bluetooth_low_energy::ScanFilterPtr filter =
      bluetooth_low_energy::ScanFilter::New();
  filter->connectable = bluetooth::Bool::New();
  filter->connectable->value = false;
  central_->StartScan(std::move(filter), [](bluetooth::Status status) {});
}

// Called when the scan state changes, e.g. when a scan session terminates due
// to a call to Central.StopScan() or another unexpected condition.
void App::OnScanStateChanged(bool scanning) {
  printf("Device %s scanning.\n", scanning ? "started" : "stopped");
}

void PrintRDHeader(const bluetooth_low_energy::RemoteDevice& device) {
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

void PrintGeneralBeaconData(const bluetooth_low_energy::RemoteDevice& device) {
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

void App::OnDeviceDiscovered(bluetooth_low_energy::RemoteDevice device) {
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
