// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <hw/reg.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-common/aml-sd-emmc.h>
#include <wifi/wifi-config.h>

#include "astro.h"

static const pbus_gpio_t wifi_gpios[] = {
    {
        .gpio = S905D2_WIFI_SDIO_WAKE_HOST,
    },
};

static const wifi_config_t wifi_config = {
    .oob_irq_mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
};

static const pbus_metadata_t wifi_metadata[] = {
    {
        .type       = DEVICE_METADATA_PRIVATE,
        .data       = &wifi_config,
        .len        = sizeof(wifi_config),
    }
};

static const pbus_boot_metadata_t wifi_boot_metadata[] = {
    {
        .zbi_type = DEVICE_METADATA_MAC_ADDRESS,
        .zbi_extra = MACADDR_WIFI,
    },
};

static const pbus_dev_t sdio_children[] = {
    {
        // Wifi driver.
        .name = "astro-wifi",
        .gpios = wifi_gpios,
        .gpio_count = countof(wifi_gpios),
        .metadata = wifi_metadata,
        .metadata_count = countof(wifi_metadata),
        .boot_metadata = wifi_boot_metadata,
        .boot_metadata_count = countof(wifi_boot_metadata),
    },
};

static const pbus_dev_t aml_sd_emmc_children[] = {
    {
        // Generic SDIO driver.
        .name = "sdio",
        .children = sdio_children,
        .child_count = countof(sdio_children),
    },
};

static const pbus_mmio_t aml_sd_emmc_mmios[] = {
    {
        .base = S905D2_EMMC_A_SDIO_BASE,
        .length = S905D2_EMMC_A_SDIO_LENGTH,
    },
};

static const pbus_irq_t aml_sd_emmc_irqs[] = {
    {
        .irq = S905D2_EMMC_A_SDIO_IRQ,
    },
};

static const pbus_bti_t aml_sd_emmc_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_SDIO,
    },
};

static const pbus_gpio_t aml_sd_emmc_gpios[] = {
    {
        .gpio = S905D2_GPIOX(6),
    },
};

static aml_sd_emmc_config_t config = {
    //PORTA on s905D2 does not support DMA.
    .supports_dma = false,
    // TODO: Astro fails the I/O requests if the frequency is more than 25MHz.
    // The same succeeds on vim2. This could probably be because of the PORT issues on astro.
    // Set the right frequency once the PORT issues are resolved.
    .max_freq = 25000000,
    .min_freq = 400000,
};

static const pbus_metadata_t aml_sd_emmc_metadata[] = {
    {
        .type       = DEVICE_METADATA_PRIVATE,
        .data       = &config,
        .len        = sizeof(config),
    }
};

static const pbus_dev_t aml_sd_emmc_dev = {
    .name = "aml-sdio",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_AMLOGIC_SD_EMMC,
    .mmios = aml_sd_emmc_mmios,
    .mmio_count = countof(aml_sd_emmc_mmios),
    .irqs = aml_sd_emmc_irqs,
    .irq_count = countof(aml_sd_emmc_irqs),
    .btis = aml_sd_emmc_btis,
    .bti_count = countof(aml_sd_emmc_btis),
    .gpios = aml_sd_emmc_gpios,
    .gpio_count = countof(aml_sd_emmc_gpios),
    .metadata = aml_sd_emmc_metadata,
    .metadata_count = countof(aml_sd_emmc_metadata),
    .children = aml_sd_emmc_children,
    .child_count = countof(aml_sd_emmc_children),
};

zx_status_t aml_sdio_init(aml_bus_t* bus) {
    zx_status_t status;

    // set alternate functions to enable EMMC
    gpio_impl_set_alt_function(&bus->gpio, S905D2_WIFI_SDIO_D0, S905D2_WIFI_SDIO_D0_FN);
    gpio_impl_set_alt_function(&bus->gpio, S905D2_WIFI_SDIO_D1, S905D2_WIFI_SDIO_D1_FN);
    gpio_impl_set_alt_function(&bus->gpio, S905D2_WIFI_SDIO_D2, S905D2_WIFI_SDIO_D2_FN);
    gpio_impl_set_alt_function(&bus->gpio, S905D2_WIFI_SDIO_D3, S905D2_WIFI_SDIO_D3_FN);
    gpio_impl_set_alt_function(&bus->gpio, S905D2_WIFI_SDIO_CLK, S905D2_WIFI_SDIO_CLK_FN);
    gpio_impl_set_alt_function(&bus->gpio, S905D2_WIFI_SDIO_CMD, S905D2_WIFI_SDIO_CMD_FN);
    gpio_impl_set_alt_function(&bus->gpio, S905D2_WIFI_SDIO_WAKE_HOST,
                               S905D2_WIFI_SDIO_WAKE_HOST_FN);
    if ((status = pbus_device_add(&bus->pbus, &aml_sd_emmc_dev)) != ZX_OK) {
        zxlogf(ERROR, "aml_sdio_init could not add aml_sd_emmc_dev: %d\n", status);
        return status;
    }

    return ZX_OK;
}
