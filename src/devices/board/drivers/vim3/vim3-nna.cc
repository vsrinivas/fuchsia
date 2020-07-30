// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/aml-a311d/a311d-hw.h>

#include "vim3.h"

namespace vim3 {

static pbus_mmio_t vim3_nna_mmios[] = {
    {
        .base = A311D_NNA_BASE,
        .length = A311D_NNA_LENGTH,
    },
    // HIU for clocks.
    {
        .base = A311D_HIU_BASE,
        .length = A311D_HIU_LENGTH,
    },
    // Power domain
    {
        .base = A311D_POWER_DOMAIN_BASE,
        .length = A311D_POWER_DOMAIN_LENGTH,
    },
    // Memory PD
    {
        .base = A311D_MEMORY_PD_BASE,
        .length = A311D_MEMORY_PD_LENGTH,
    },
    // AXI SRAM
    {
        .base = A311D_NNA_SRAM_BASE,
        .length = A311D_NNA_SRAM_LENGTH,
    },
};

static pbus_bti_t nna_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_NNA,
    },
};

static pbus_irq_t nna_irqs[] = {
    {
        .irq = A311D_NNA_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
};

static pbus_dev_t nna_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-nna";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_A311D;
  dev.did = PDEV_DID_AMLOGIC_NNA;
  dev.mmio_list = vim3_nna_mmios;
  dev.mmio_count = countof(vim3_nna_mmios);
  dev.bti_list = nna_btis;
  dev.bti_count = countof(nna_btis);
  dev.irq_list = nna_irqs;
  dev.irq_count = countof(nna_irqs);
  return dev;
}();

zx_status_t Vim3::NnaInit() {
  zx_status_t status = pbus_.DeviceAdd(&nna_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Vim3::NnaInit: pbus_device_add() failed for nna: %d", status);
    return status;
  }
  return ZX_OK;
}

}  // namespace vim3
