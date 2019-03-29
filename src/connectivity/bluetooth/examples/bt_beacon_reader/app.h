// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_EXAMPLES_BT_BEACON_READER_APP_H_
#define SRC_CONNECTIVITY_BLUETOOTH_EXAMPLES_BT_BEACON_READER_APP_H_

#include <fuchsia/bluetooth/le/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/component_context.h>

#include "src/lib/fxl/macros.h"

namespace bt_beacon_reader {

class App final : public fuchsia::bluetooth::le::CentralDelegate {
 public:
  App(async::Loop* loop, bool just_tilts);

  void StartScanning();

 private:
  // fuchsia::bluetooth::le::CentralDelegate overrides:
  void OnScanStateChanged(bool scanning);
  void OnDeviceDiscovered(fuchsia::bluetooth::le::RemoteDevice device);
  void OnPeripheralDisconnected(::fidl::StringPtr identifier);

  async::Loop* const loop_;
  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::bluetooth::le::CentralPtr central_;

  // Local CentralDelegate binding.
  fidl::Binding<fuchsia::bluetooth::le::CentralDelegate> central_delegate_;

  bool just_tilts_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace bt_beacon_reader

#endif  // SRC_CONNECTIVITY_BLUETOOTH_EXAMPLES_BT_BEACON_READER_APP_H_
