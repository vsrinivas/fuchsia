// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>

#include "qemu-bus.h"
#include "qemu-virt.h"

namespace board_qemu_arm64 {

zx_status_t QemuArm64::RtcInit() {
  constexpr pbus_mmio_t kPl031Mmios[] = {
      {
          .base = RTC_BASE_PHYS,
          .length = RTC_SIZE,
      },
  };
  pbus_dev_t pl031_dev = {};
  pl031_dev.name = "pl031";
  pl031_dev.vid = PDEV_VID_GENERIC;
  pl031_dev.pid = PDEV_PID_GENERIC;
  pl031_dev.did = PDEV_DID_RTC_PL031;
  pl031_dev.mmio_list = kPl031Mmios;
  pl031_dev.mmio_count = countof(kPl031Mmios);

  auto status = pbus_.DeviceAdd(&pl031_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_qemu_arm64
