// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/metadata.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <hw/reg.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <unistd.h>

#include "astro.h"

static const pbus_mmio_t raw_nand_mmios[] = {
    {   /* nandreg : Registers for NAND controller */
        .base = S905D2_RAW_NAND_REG_BASE,
        .length = 0x2000,
    },
    {   /* clockreg : Clock Register for NAND controller */
        .base = S905D2_RAW_NAND_CLOCK_BASE,
        .length = 0x4,  /* Just 4 bytes */
    },
};

static const pbus_irq_t raw_nand_irqs[] = {
    {
        .irq = S905D2_RAW_NAND_IRQ,
    },
};

static const pbus_bti_t raw_nand_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_AML_RAW_NAND,
    },
};

static const pbus_boot_metadata_t raw_nand_metadata[] = {
    {
        .type = DEVICE_METADATA_PARTITION_MAP,
        .extra = 0,
    },
};

static const pbus_dev_t raw_nand_dev = {
    .name = "aml_raw_nand",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_AMLOGIC_RAW_NAND,
    .mmios = raw_nand_mmios,
    .mmio_count = countof(raw_nand_mmios),
    .irqs = raw_nand_irqs,
    .irq_count = countof(raw_nand_irqs),
    .btis = raw_nand_btis,
    .bti_count = countof(raw_nand_btis),
    .boot_metadata = raw_nand_metadata,
    .boot_metadata_count = countof(raw_nand_metadata),
};

zx_status_t aml_raw_nand_init(aml_bus_t* bus) {
    zx_status_t status;

    // Set alternate functions to enable raw_nand.
    status = gpio_set_alt_function(&bus->gpio, S905D2_GPIOBOOT(8), 2);
    if (status != ZX_OK)
        return status;
    status = gpio_set_alt_function(&bus->gpio, S905D2_GPIOBOOT(9), 2);
    if (status != ZX_OK)
        return status;
    status = gpio_set_alt_function(&bus->gpio, S905D2_GPIOBOOT(10), 2);
    if (status != ZX_OK)
        return status;
    status = gpio_set_alt_function(&bus->gpio, S905D2_GPIOBOOT(11), 2);
    if (status != ZX_OK)
        return status;
    status = gpio_set_alt_function(&bus->gpio, S905D2_GPIOBOOT(12), 2);
    if (status != ZX_OK)
        return status;
    status = gpio_set_alt_function(&bus->gpio, S905D2_GPIOBOOT(14), 2);
    if (status != ZX_OK)
        return status;
    status = gpio_set_alt_function(&bus->gpio, S905D2_GPIOBOOT(15), 2);
    if (status != ZX_OK)
        return status;

    status = pbus_device_add(&bus->pbus, &raw_nand_dev, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pbus_device_add failed: %d\n",
               __func__, status);
        return status;
    }

    return ZX_OK;
}

