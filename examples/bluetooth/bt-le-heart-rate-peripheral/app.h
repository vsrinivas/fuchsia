// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_BLUETOOTH_BT_LE_HEART_RATE_PERIPHERAL_APP_H_
#define GARNET_EXAMPLES_BLUETOOTH_BT_LE_HEART_RATE_PERIPHERAL_APP_H_

#include <memory>
#include <unordered_map>

#include <bluetooth_gatt/cpp/fidl.h>
#include <bluetooth_low_energy/cpp/fidl.h>
#include <lib/app/cpp/application_context.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/string.h>

#include "service.h"

namespace bt_le_heart_rate {

class App final : public bluetooth_low_energy::PeripheralDelegate {
 public:
  explicit App(std::unique_ptr<HeartModel> heart_model);
  ~App() = default;

  void StartAdvertising();

  Service* service() { return &service_; }

 private:
  using Binding = fidl::Binding<bluetooth_low_energy::PeripheralDelegate>;

  static constexpr char kDeviceName[] = "FX BLE Heart Rate";

  void OnCentralConnected(fidl::StringPtr advertisement_id,
                          bluetooth_low_energy::RemoteDevice central) override;
  void OnCentralDisconnected(fidl::StringPtr device_id) override;

  // Application
  std::unique_ptr<component::ApplicationContext> context_;

  // GATT
  Service service_;
  bluetooth_gatt::ServerPtr gatt_server_;

  // BLE advertisement
  std::unique_ptr<Binding> binding_;  // For current advertisement
  std::unordered_map<std::string, std::unique_ptr<Binding>> connected_bindings_;
  bluetooth_low_energy::PeripheralPtr peripheral_;
};

}  // namespace bt_le_heart_rate

#endif  // GARNET_EXAMPLES_BLUETOOTH_BT_LE_HEART_RATE_PERIPHERAL_APP_H_
