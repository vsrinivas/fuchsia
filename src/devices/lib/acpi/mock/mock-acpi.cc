// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/acpi/mock/mock-acpi.h"

namespace acpi::mock {

zx::status<acpi::Client> Device::CreateClient(async_dispatcher_t *dispatcher) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_acpi::Device>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }

  fidl::BindServer(dispatcher, std::move(endpoints->server), this);
  return zx::ok(acpi::Client::Create(
      fidl::WireSyncClient<fuchsia_hardware_acpi::Device>(std::move(endpoints->client))));
}

}  // namespace acpi::mock
