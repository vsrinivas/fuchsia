// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include "sherlock.h"

namespace sherlock {

static const pbus_mmio_t sherlock_ram_ctl_mmios[] = {
    {
        .base = T931_DMC_BASE,
        .length = T931_DMC_LENGTH,
    },
};

static const pbus_irq_t sherlock_ram_ctl_irqs[] = {
    {
        .irq = T931_DMC_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_dev_t ramctl_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-ram-ctl";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_T931;
  dev.did = PDEV_DID_AMLOGIC_RAM_CTL;
  dev.mmio_list = sherlock_ram_ctl_mmios;
  dev.mmio_count = std::size(sherlock_ram_ctl_mmios);
  dev.irq_list = sherlock_ram_ctl_irqs;
  dev.irq_count = std::size(sherlock_ram_ctl_irqs);
  return dev;
}();

zx_status_t Sherlock::RamCtlInit() {
  zx_status_t status = pbus_.DeviceAdd(&ramctl_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed: %d", __func__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace sherlock
