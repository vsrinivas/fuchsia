// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/cpp/bluetooth_low_energy.h>
#include "lib/app/cpp/application_context.h"
#include "lib/fxl/macros.h"

namespace bt_beacon_reader {

class App final : public bluetooth_low_energy::CentralDelegate {
 public:
  App(bool just_tilts);

  void StartScanning();

 private:
  // bluetooth_low_energy::CentralDelegate overrides:
  void OnScanStateChanged(bool scanning);
  void OnDeviceDiscovered(bluetooth_low_energy::RemoteDevice device);
  void OnPeripheralDisconnected(::fidl::StringPtr identifier);

  std::unique_ptr<component::ApplicationContext> context_;
  bluetooth_low_energy::CentralPtr central_;

  // Local CentralDelegate binding.
  fidl::Binding<bluetooth_low_energy::CentralDelegate> central_delegate_;

  bool just_tilts_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace bt_beacon_reader
