// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_LIB_ACPI_CLIENT_H_
#define SRC_DEVICES_LIB_ACPI_CLIENT_H_

#include <fuchsia/hardware/acpi/llcpp/fidl.h>
#include <lib/ddk/driver.h>
#include <lib/zx/status.h>

#include "src/devices/lib/acpi/util.h"

namespace acpi {
// This class is a wrapper around the underlying FIDL ACPI protocol.
// It provides helper functions to make calling common ACPI methods more ergonomic.
class Client {
 public:
  // Connect to the ACPI FIDL server by calling ConnectServer() on the "acpi" fragment of |parent|.
  static zx::status<Client> Create(zx_device_t* parent);
  // Alternate constructor mainly intended for use in unit tests.
  static Client Create(fidl::WireSyncClient<fuchsia_hardware_acpi::Device> client);

  // Borrow the underlying FIDL client.
  fidl::WireSyncClient<fuchsia_hardware_acpi::Device>& borrow() { return client_; }

  // This calls _DSM on the device with the given arguments. See
  // ACPI Spec 6.4, 9.1.1 "_DSM (Device Specific Method)" for more information.
  zx::status<fuchsia_hardware_acpi::wire::DeviceEvaluateObjectResult> CallDsm(
      Uuid uuid, uint64_t revision, uint64_t func_index,
      std::optional<fuchsia_hardware_acpi::wire::Object> params);

 private:
  explicit Client(fidl::WireSyncClient<fuchsia_hardware_acpi::Device> client)
      : client_(std::move(client)) {}
  fidl::WireSyncClient<fuchsia_hardware_acpi::Device> client_;
};

}  // namespace acpi

#endif  // SRC_DEVICES_LIB_ACPI_CLIENT_H_
