// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <ddk/debug.h>
#include <zircon/errors.h>

#include "rtl8821c_device.h"
#include "rtl88xx_registers.h"

namespace wlan {
namespace rtl88xx {

Device::~Device() {}

// static
zx_status_t Device::Create(std::unique_ptr<Bus> bus, std::unique_ptr<Device>* device) {
  zx_status_t status = ZX_OK;

  reg::SYS_CFG2 cfg2;
  if ((status = bus->ReadRegister(&cfg2)) != ZX_OK) {
    return status;
  }
  switch (cfg2.hw_id()) {
    case reg::SYS_CFG2::HwId::HW_ID_8821C:
      return Rtl8821cDevice::Create(std::move(bus), device);
    default:
      zxlogf(ERROR, "rtl88xx: Device::Create() not supported for hw_id=%04x", cfg2.hw_id());
      return ZX_ERR_NOT_SUPPORTED;
  }
}

}  // namespace rtl88xx
}  // namespace wlan
