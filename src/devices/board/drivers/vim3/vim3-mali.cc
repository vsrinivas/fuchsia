// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/platform-defs.h>
#include <lib/ddk/hw/reg.h>

#include <soc/aml-a311d/a311d-hw.h>
#include <soc/aml-common/aml-registers.h>

#include "vim3.h"

namespace vim3 {
static const pbus_mmio_t mali_mmios[] = {
    {
        .base = A311D_MALI_BASE,
        .length = A311D_MALI_LENGTH,
    },
    {
        .base = A311D_HIU_BASE,
        .length = A311D_HIU_LENGTH,
    },
};

static const pbus_irq_t mali_irqs[] = {
    {
        .irq = A311D_MALI_IRQ_PP,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = A311D_MALI_IRQ_GPMMU,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = A311D_MALI_IRQ_GP,
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
  dev.pid = PDEV_PID_AMLOGIC_A311D;
  dev.did = PDEV_DID_AMLOGIC_MALI_INIT;
  dev.mmio_list = mali_mmios;
  dev.mmio_count = countof(mali_mmios);
  dev.irq_list = mali_irqs;
  dev.irq_count = countof(mali_irqs);
  dev.bti_list = mali_btis;
  dev.bti_count = countof(mali_btis);
  return dev;
}();

static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t reset_register_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_REGISTERS),
    BI_MATCH_IF(EQ, BIND_REGISTER_ID, aml_registers::REGISTER_MALI_RESET),
};
static const device_fragment_part_t reset_register_fragment[] = {
    {countof(root_match), root_match},
    {countof(reset_register_match), reset_register_match},
};
static const device_fragment_t mali_fragments[] = {
    {"register-reset", countof(reset_register_fragment), reset_register_fragment},
};

zx_status_t Vim3::MaliInit() {
  zx_status_t status = pbus_.CompositeDeviceAdd(
      &mali_dev, reinterpret_cast<uint64_t>(mali_fragments), countof(mali_fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Sherlock::MaliInit: CompositeDeviceAdd failed: %d", status);
    return status;
  }
  return status;
}

}  // namespace vim3
