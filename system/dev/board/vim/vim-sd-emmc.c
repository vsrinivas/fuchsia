// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/protocol/platform-defs.h>
#include <hw/reg.h>
#include <soc/aml-s912/s912-hw.h>
#include <soc/aml-a113/a113-hw.h>
#include <soc/aml-s912/s912-gpio.h>
#include <soc/aml-common/aml-sd-emmc.h>

#include "vim.h"

#define BIT_MASK(start, count) (((1 << (count)) - 1) << (start))
#define SET_BITS(dest, start, count, value) \
        ((dest & ~BIT_MASK(start, count)) | (((value) << (start)) & BIT_MASK(start, count)))

/*static const pbus_mmio_t sdio_mmios[] = {
    {
        .base = 0xD0070000,
        .length = 0x2000,
    }
};

static const pbus_mmio_t sd_mmios[] = {
    {
        .base = 0xD0072000,
        .length = 0x2000,
    }
};*/

/*static const pbus_irq_t sdio_irqs[] = {
    {
        .irq = 248,
    },
};

static const pbus_irq_t sd_irqs[] = {
    {
        .irq = 249,
    },
};*/

static const pbus_mmio_t emmc_mmios[] = {
    {
        .base = 0xD0074000,
        .length = 0x2000,
    }
};

static const pbus_irq_t emmc_irqs[] = {
    {
        .irq = 250,
    },
};

static const pbus_bti_t emmc_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_EMMC,
    },
};

static const pbus_gpio_t emmc_gpios[] = {
    {
        .gpio = S912_EMMC_RST,
    },
};

static aml_sd_emmc_config_t config = {
    .supports_dma = true,
    //As per AMlogic, on S912 chipset, HS400 mode can be operated at 125MHZ or low.
    .min_freq = 400000,
    .max_freq = 120000000,
};

static const pbus_metadata_t emmc_metadata[] = {
    {
        .type       = DEVICE_METADATA_PRIVATE,
        .extra      = 0,
        .data       = &config,
        .len        = sizeof(config),
    },
    {
        .type = DEVICE_METADATA_PARTITION_MAP,
        .extra = 0,
    },
};

static const pbus_dev_t emmc_dev = {
    .name = "aml_emmc",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_AMLOGIC_SD_EMMC,
    .mmios = emmc_mmios,
    .mmio_count = countof(emmc_mmios),
    .irqs = emmc_irqs,
    .irq_count = countof(emmc_irqs),
    .btis = emmc_btis,
    .bti_count = countof(emmc_btis),
    .gpios = emmc_gpios,
    .gpio_count = countof(emmc_gpios),
    .metadata = emmc_metadata,
    .metadata_count = countof(emmc_metadata),
};

zx_status_t vim_sd_emmc_init(vim_bus_t* bus) {
    zx_status_t status;

    // set alternate functions to enable EMMC
    gpio_set_alt_function(&bus->gpio, S912_EMMC_NAND_D0, S912_EMMC_NAND_D0_FN);
    gpio_set_alt_function(&bus->gpio, S912_EMMC_NAND_D1, S912_EMMC_NAND_D1_FN);
    gpio_set_alt_function(&bus->gpio, S912_EMMC_NAND_D2, S912_EMMC_NAND_D2_FN);
    gpio_set_alt_function(&bus->gpio, S912_EMMC_NAND_D3, S912_EMMC_NAND_D3_FN);
    gpio_set_alt_function(&bus->gpio, S912_EMMC_NAND_D4, S912_EMMC_NAND_D4_FN);
    gpio_set_alt_function(&bus->gpio, S912_EMMC_NAND_D5, S912_EMMC_NAND_D5_FN);
    gpio_set_alt_function(&bus->gpio, S912_EMMC_NAND_D6, S912_EMMC_NAND_D6_FN);
    gpio_set_alt_function(&bus->gpio, S912_EMMC_NAND_D7, S912_EMMC_NAND_D7_FN);
    gpio_set_alt_function(&bus->gpio, S912_EMMC_CLK, S912_EMMC_CLK_FN);
    gpio_set_alt_function(&bus->gpio, S912_EMMC_RST, S912_EMMC_RST_FN);
    gpio_set_alt_function(&bus->gpio, S912_EMMC_CMD, S912_EMMC_CMD_FN);
    gpio_set_alt_function(&bus->gpio, S912_EMMC_DS, S912_EMMC_DS_FN);

    if ((status = pbus_device_add(&bus->pbus, &emmc_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "vim_sd_emmc_init could not add emmc_dev: %d\n", status);
        return status;
    }

    return ZX_OK;
}
