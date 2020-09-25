// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bus.h"

#include <ddk/debug.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include "usb_bus.h"

namespace wlan {
namespace rtl88xx {

Bus::~Bus() {}

// static
zx_status_t Bus::Create(zx_device_t* bus_device, std::unique_ptr<Bus>* bus) {
  const zx_status_t usb_status = CreateUsbBus(bus_device, bus);
  if (usb_status == ZX_OK) {
    return ZX_OK;
  }

  zxlogf(ERROR, "rtl88xx: CreateUsbBus() returned %s", zx_status_get_string(usb_status));
  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace rtl88xx
}  // namespace wlan
