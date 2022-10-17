// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fragment-irq.h"

#include <fidl/fuchsia.hardware.interrupt/cpp/markers.h>
#include <fidl/fuchsia.hardware.interrupt/cpp/wire_messaging.h>
#include <lib/ddk/debug.h>

#include <ddktl/device.h>
#include <fbl/string_printf.h>

namespace fragment_irq {
namespace fint = fuchsia_hardware_interrupt;

zx::result<zx::interrupt> GetInterrupt(zx_device_t* dev, const char* fragment_name) {
  auto client_end = ddk::Device<void>::DdkConnectFragmentFidlProtocol<fint::Service::Provider>(
      dev, fragment_name);
  if (client_end.is_error()) {
    zxlogf(WARNING, "Failed to connect to fragment '%s' service '%s' protocol '%s': %s",
           fragment_name, fint::Service::Provider::ServiceName, fint::Service::Provider::Name,
           client_end.status_string());
    return client_end.take_error();
  }

  auto result = fidl::WireCall(*client_end)->Get();
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

zx::result<zx::interrupt> GetInterrupt(zx_device_t* dev, uint32_t which) {
  return GetInterrupt(dev, fbl::StringPrintf("irq%03u", which).data());
}

}  // namespace fragment_irq
