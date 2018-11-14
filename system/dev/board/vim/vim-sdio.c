// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <hw/reg.h>
#include <soc/aml-s912/s912-hw.h>
#include <soc/aml-a113/a113-hw.h>
#include <soc/aml-s912/s912-gpio.h>
#include <soc/aml-common/aml-sd-emmc.h>
#include <wifi/wifi-config.h>

#include "vim.h"

static const pbus_gpio_t wifi_gpios[] = {
    {
        .gpio = S912_WIFI_SDIO_WAKE_HOST,
    },
    {
        // For debugging purposes.
        .gpio = S912_GPIODV(13),
    },
};

static const wifi_config_t wifi_config = {
    .oob_irq_mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
};

static const pbus_metadata_t wifi_metadata[] = {
    {
        .type       = DEVICE_METADATA_PRIVATE,
        .data_buffer       = &wifi_config,
        .data_size        = sizeof(wifi_config),
    }
};

static const pbus_dev_t sdio_children[] = {
    {
        // Wifi driver.
        .name = "vim2-wifi",
        .gpio_list = wifi_gpios,
        .gpio_count = countof(wifi_gpios),
        .metadata_list = wifi_metadata,
        .metadata_count = countof(wifi_metadata),
    },
};

static const pbus_dev_t aml_sd_emmc_children[] = {
    {
        // Generic SDIO driver.
        .name = "sdio",
        .child_list = sdio_children,
        .child_count = countof(sdio_children),
    },
};

static const pbus_mmio_t aml_sd_emmc_mmios[] = {
    {
        .base = 0xD0070000,
        .length = 0x2000,
    }
};

static const pbus_irq_t aml_sd_emmc_irqs[] = {
    {
        .irq = 248,
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
        .gpio = S912_GPIOX(6),
    },
};

static aml_sd_emmc_config_t config = {
    .supports_dma = true,
    .max_freq = 125000000,
    .min_freq = 400000,
};

static const pbus_metadata_t aml_sd_emmc_metadata[] = {
    {
        .type       = DEVICE_METADATA_PRIVATE,
        .data_buffer       = &config,
        .data_size        = sizeof(config),
    }
};

static const pbus_dev_t aml_sd_emmc_dev = {
    .name = "aml-sdio",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_AMLOGIC_SD_EMMC,
    .mmio_list = aml_sd_emmc_mmios,
    .mmio_count = countof(aml_sd_emmc_mmios),
    .irq_list = aml_sd_emmc_irqs,
    .irq_count = countof(aml_sd_emmc_irqs),
    .bti_list = aml_sd_emmc_btis,
    .bti_count = countof(aml_sd_emmc_btis),
    .gpio_list = aml_sd_emmc_gpios,
    .gpio_count = countof(aml_sd_emmc_gpios),
    .metadata_list = aml_sd_emmc_metadata,
    .metadata_count = countof(aml_sd_emmc_metadata),
    .child_list = aml_sd_emmc_children,
    .child_count = countof(aml_sd_emmc_children),
};

zx_status_t vim_sdio_init(vim_bus_t* bus) {
    zx_status_t status;

    gpio_impl_set_alt_function(&bus->gpio, S912_WIFI_SDIO_D0, S912_WIFI_SDIO_D0_FN);
    gpio_impl_set_alt_function(&bus->gpio, S912_WIFI_SDIO_D1, S912_WIFI_SDIO_D1_FN);
    gpio_impl_set_alt_function(&bus->gpio, S912_WIFI_SDIO_D2, S912_WIFI_SDIO_D2_FN);
    gpio_impl_set_alt_function(&bus->gpio, S912_WIFI_SDIO_D3, S912_WIFI_SDIO_D3_FN);
    gpio_impl_set_alt_function(&bus->gpio, S912_WIFI_SDIO_CLK, S912_WIFI_SDIO_CLK_FN);
    gpio_impl_set_alt_function(&bus->gpio, S912_WIFI_SDIO_CMD, S912_WIFI_SDIO_CMD_FN);
    gpio_impl_set_alt_function(&bus->gpio, S912_WIFI_SDIO_WAKE_HOST, S912_WIFI_SDIO_WAKE_HOST_FN);

    if ((status = pbus_device_add(&bus->pbus, &aml_sd_emmc_dev)) != ZX_OK) {
        zxlogf(ERROR, "vim_sdio_init could not add aml_sd_emmc_dev: %d\n", status);
        return status;
    }

    return ZX_OK;
}
