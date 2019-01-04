// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <hw/reg.h>
#include <soc/aml-s912/s912-hw.h>

#include "vim.h"

static const pbus_mmio_t mali_mmios[] = {
    {
        .base = S912_MALI_BASE,
        .length = S912_MALI_LENGTH,
    },
    {
        .base = S912_HIU_BASE,
        .length = S912_HIU_LENGTH,
    },
    {
        .base = S912_PRESET_BASE,
        .length = S912_PRESET_LENGTH,
    },
};

static const pbus_irq_t mali_irqs[] = {
    {
        .irq = S912_MALI_IRQ_PP,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = S912_MALI_IRQ_GPMMU,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = S912_MALI_IRQ_GP,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
};

static pbus_bti_t mali_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = 0,
    },
};

static const pbus_dev_t mali_dev = {
    .name = "mali",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_S912,
    .did = PDEV_DID_ARM_MALI_INIT,
    .mmio_list = mali_mmios,
    .mmio_count = countof(mali_mmios),
    .irq_list = mali_irqs,
    .irq_count = countof(mali_irqs),
    .bti_list = mali_btis,
    .bti_count = countof(mali_btis),
};

zx_status_t vim_mali_init(vim_bus_t* bus, uint32_t bti_index) {

    // Populate the BTI information
    mali_btis[0].iommu_index = 0;
    mali_btis[0].bti_id      = bti_index;

    zx_status_t status = pbus_device_add(&bus->pbus, &mali_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_mali_init: pbus_device_add failed: %d\n", status);
        return status;
    }
    return status;
}
