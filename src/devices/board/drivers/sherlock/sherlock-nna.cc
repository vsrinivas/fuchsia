// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"

namespace sherlock {

static pbus_mmio_t sherlock_nna_mmios[] = {
    {
        .base = T931_NNA_BASE,
        .length = T931_NNA_LENGTH,
    },
    // HIU for clocks.
    {
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    },
    // Power domain
    {
        .base = T931_POWER_DOMAIN_BASE,
        .length = T931_POWER_DOMAIN_LENGTH,
    },
    // Memory PD
    {
        .base = T931_MEMORY_PD_BASE,
        .length = T931_MEMORY_PD_LENGTH,
    },
    // AXI SRAM
    {
        .base = T931_NNA_SRAM_BASE,
        .length = T931_NNA_SRAM_LENGTH,
    },
    // Reset
    {
        .base = T931_RESET_BASE,
        .length = T931_RESET_LENGTH,
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
        .irq = T931_NNA_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
};

static uint64_t s_external_sram_phys_base = T931_NNA_SRAM_BASE;

static pbus_metadata_t nna_metadata[] = {
    {
        .type = 0,
        .data_buffer = &s_external_sram_phys_base,
        .data_size = sizeof(s_external_sram_phys_base),
    },
};

static pbus_dev_t nna_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-nna";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_T931;
  dev.did = PDEV_DID_AMLOGIC_NNA;
  dev.mmio_list = sherlock_nna_mmios;
  dev.mmio_count = countof(sherlock_nna_mmios);
  dev.bti_list = nna_btis;
  dev.bti_count = countof(nna_btis);
  dev.irq_list = nna_irqs;
  dev.irq_count = countof(nna_irqs);
  dev.metadata_list = nna_metadata;
  dev.metadata_count = countof(nna_metadata);
  return dev;
}();

zx_status_t Sherlock::NnaInit() {
  zx_status_t status = pbus_.DeviceAdd(&nna_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Sherlock::NnaInit: pbus_device_add() failed for nna: %d", status);
    return status;
  }
  return ZX_OK;
}

}  // namespace sherlock
