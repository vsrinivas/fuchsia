// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_BLUETOOTH_BT_LE_HEART_RATE_PERIPHERAL_APP_H_
#define GARNET_EXAMPLES_BLUETOOTH_BT_LE_HEART_RATE_PERIPHERAL_APP_H_

#include <memory>

#include <fuchsia/bluetooth/gatt/cpp/fidl.h>
#include <fuchsia/bluetooth/le/cpp/fidl.h>
#include <lib/app/cpp/startup_context.h>
#include <lib/fidl/cpp/string.h>

#include "service.h"

namespace bt_le_heart_rate {

class App final {
 public:
  explicit App(std::unique_ptr<HeartModel> heart_model);
  ~App() = default;

  void StartAdvertising();

  Service* service() { return &service_; }

 private:
  static constexpr char kDeviceName[] = "FX BLE Heart Rate";

  void OnCentralConnected(fidl::StringPtr advertisement_id,
                          fuchsia::bluetooth::le::RemoteDevice central);
  void OnCentralDisconnected(fidl::StringPtr device_id);

  // Application
  std::unique_ptr<fuchsia::sys::StartupContext> context_;

  // GATT
  Service service_;
  fuchsia::bluetooth::gatt::ServerPtr gatt_server_;

  // BLE advertisement
  fuchsia::bluetooth::le::PeripheralPtr peripheral_;
};

}  // namespace bt_le_heart_rate

#endif  // GARNET_EXAMPLES_BLUETOOTH_BT_LE_HEART_RATE_PERIPHERAL_APP_H_
