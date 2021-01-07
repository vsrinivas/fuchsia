// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <hw/reg.h>
#include <soc/aml-common/aml-registers.h>
#include <soc/aml-s905d3/s905d3-hw.h>

#include "nelson.h"

namespace nelson {

static const pbus_mmio_t nna_mmios[] = {
    {
        .base = S905D3_NNA_BASE,
        .length = S905D3_NNA_LENGTH,
    },
    // HIU for clocks.
    {
        .base = S905D3_HIU_BASE,
        .length = S905D3_HIU_LENGTH,
    },
    // Power domain
    {
        .base = S905D3_POWER_DOMAIN_BASE,
        .length = S905D3_POWER_DOMAIN_LENGTH,
    },
    // Memory PD
    {
        .base = S905D3_MEMORY_PD_BASE,
        .length = S905D3_MEMORY_PD_LENGTH,
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
        .irq = S905D3_NNA_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
};

static pbus_dev_t nna_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-nna";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_S905D3;
  dev.did = PDEV_DID_AMLOGIC_NNA;
  dev.mmio_list = nna_mmios;
  dev.mmio_count = countof(nna_mmios);
  dev.bti_list = nna_btis;
  dev.bti_count = countof(nna_btis);
  dev.irq_list = nna_irqs;
  dev.irq_count = countof(nna_irqs);
  return dev;
}();

static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};

static const zx_bind_inst_t reset_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_REGISTERS),
    BI_MATCH_IF(EQ, BIND_REGISTER_ID, aml_registers::REGISTER_NNA_RESET_LEVEL2),
};

static const device_fragment_part_t reset_fragment[] = {
    {countof(root_match), root_match},
    {countof(reset_match), reset_match},
};

static const device_fragment_t fragments[] = {
    {"register-reset", countof(reset_fragment), reset_fragment},
};

zx_status_t Nelson::NnaInit() {
  zx_status_t status =
      pbus_.CompositeDeviceAdd(&nna_dev, fragments, countof(fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Nelson::NnaInit: pbus_device_add() failed for nna: %d", status);
    return status;
  }
  return ZX_OK;
}

}  // namespace nelson
