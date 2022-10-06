// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fragment-irq.h"

#include <fidl/fuchsia.hardware.interrupt/cpp/markers.h>
#include <fidl/fuchsia.hardware.interrupt/cpp/wire_messaging.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>

#include <fbl/string_printf.h>

namespace fragment_irq {
namespace fint = fuchsia_hardware_interrupt;

zx::status<zx::interrupt> GetInterrupt(zx_device_t* dev, const char* fragment_name) {
  auto endpoints = fidl::CreateEndpoints<fint::Service::Provider::ProtocolType>();
  if (endpoints.is_error()) {
    zxlogf(ERROR, "Failed to create endpoints: %s", endpoints.status_string());
    return endpoints.take_error();
  }

  zx_status_t status = device_connect_fragment_fidl_protocol2(
      dev, fragment_name, fint::Service::Provider::ServiceName, fint::Service::Provider::Name,
      endpoints->server.TakeChannel().release());
  if (status != ZX_OK) {
    zxlogf(WARNING, "Failed to connect to fragment '%s' service '%s' protocol '%s': %s",
           fragment_name, fint::Service::Provider::ServiceName, fint::Service::Provider::Name,
           zx_status_get_string(status));
    return zx::error(status);
  }

  auto result = fidl::WireCall(endpoints->client)->Get();
  if (!result.ok()) {
    zxlogf(ERROR, "Failed to send Get() request: %s", result.status_string());
    return zx::error(result.status());
  }
  if (result->is_error()) {
    zxlogf(ERROR, "Failed to Get(): %s", zx_status_get_string(result->error_value()));
    return zx::error(result->error_value());
  }

  return zx::ok(std::move(result->value()->interrupt));
}

zx::status<zx::interrupt> GetInterrupt(zx_device_t* dev, uint32_t which) {
  return GetInterrupt(dev, fbl::StringPrintf("irq%03u", which).data());
}

}  // namespace fragment_irq
