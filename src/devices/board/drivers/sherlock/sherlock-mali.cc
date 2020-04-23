// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <hw/reg.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"

namespace sherlock {
static const pbus_mmio_t mali_mmios[] = {
    {
        .base = T931_MALI_BASE,
        .length = T931_MALI_LENGTH,
    },
    {
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    },
    {
        .base = T931_RESET_BASE,
        .length = T931_RESET_LENGTH,
    },
};

static const pbus_irq_t mali_irqs[] = {
    {
        .irq = T931_MALI_IRQ_PP,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = T931_MALI_IRQ_GPMMU,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = T931_MALI_IRQ_GP,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
};

static pbus_bti_t mali_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_MALI,
    },
};

static pbus_dev_t mali_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "mali";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_T931;
  dev.did = PDEV_DID_AMLOGIC_MALI_INIT;
  dev.mmio_list = mali_mmios;
  dev.mmio_count = countof(mali_mmios);
  dev.irq_list = mali_irqs;
  dev.irq_count = countof(mali_irqs);
  dev.bti_list = mali_btis;
  dev.bti_count = countof(mali_btis);
  return dev;
}();

zx_status_t Sherlock::MaliInit() {
  zx_status_t status = pbus_.DeviceAdd(&mali_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Sherlock::MaliInit: pbus_device_add failed: %d", status);
    return status;
  }
  return status;
}

}  // namespace sherlock
