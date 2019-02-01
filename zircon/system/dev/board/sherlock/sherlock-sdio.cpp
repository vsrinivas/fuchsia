// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <lib/mmio/mmio.h>
#include <ddktl/protocol/gpioimpl.h>
#include <hw/reg.h>
#include <lib/zx/handle.h>
#include <soc/aml-common/aml-sd-emmc.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>
#include <wifi/wifi-config.h>

#include <optional>

#include "sherlock.h"

namespace sherlock {

namespace {

constexpr pbus_gpio_t wifi_gpios[] = {
    {
        .gpio = T931_WIFI_HOST_WAKE,
    },
};

constexpr wifi_config_t wifi_config = {
    .oob_irq_mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
};

constexpr pbus_metadata_t wifi_metadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = &wifi_config,
        .data_size = sizeof(wifi_config),
    },
};

constexpr pbus_boot_metadata_t wifi_boot_metadata[] = {
    {
        .zbi_type = DEVICE_METADATA_MAC_ADDRESS,
        .zbi_extra = MACADDR_WIFI,
    },
};

const pbus_dev_t sdio_children[] = {
    []() {
        pbus_dev_t dev;
        dev.name = "sherlock-wifi";
        dev.gpio_list = wifi_gpios;
        dev.gpio_count = countof(wifi_gpios);
        dev.metadata_list = wifi_metadata;
        dev.metadata_count = countof(wifi_metadata);
        dev.boot_metadata_list = wifi_boot_metadata;
        dev.boot_metadata_count = countof(wifi_boot_metadata);
        return dev;
    }(),
};

const pbus_dev_t sd_emmc_children[] = {
    []() {
        pbus_dev_t dev;
        dev.name = "sherlock-sdio";
        dev.child_list = sdio_children;
        dev.child_count = countof(sdio_children);
        return dev;
    }(),
};

constexpr pbus_mmio_t sd_emmc_mmios[] = {
    {
        .base = T931_SD_EMMC_A_BASE,
        .length = T931_SD_EMMC_A_LENGTH,
    },
};

constexpr pbus_irq_t sd_emmc_irqs[] = {
    {
        .irq = T931_SD_EMMC_A_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

constexpr pbus_bti_t sd_emmc_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_SDIO,
    },
};

constexpr pbus_gpio_t sd_emmc_gpios[] = {
    {
        .gpio = T931_WIFI_REG_ON,
    },
};

constexpr aml_sd_emmc_config_t sd_emmc_config = {
    .supports_dma = false,
    .min_freq = 500000,   // 500KHz
    .max_freq = 50000000, // 50MHz
};

constexpr pbus_metadata_t sd_emmc_metadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = &sd_emmc_config,
        .data_size = sizeof(sd_emmc_config),
    },
};

const pbus_dev_t sdio_dev = []() {
    pbus_dev_t dev;
    dev.name = "sherlock-sd-emmc";
    dev.vid = PDEV_VID_AMLOGIC;
    dev.pid = PDEV_PID_GENERIC;
    dev.did = PDEV_DID_AMLOGIC_SD_EMMC;
    dev.mmio_list = sd_emmc_mmios;
    dev.mmio_count = countof(sd_emmc_mmios);
    dev.bti_list = sd_emmc_btis;
    dev.bti_count = countof(sd_emmc_btis);
    dev.irq_list = sd_emmc_irqs;
    dev.irq_count = countof(sd_emmc_irqs);
    dev.gpio_list = sd_emmc_gpios,
    dev.gpio_count = countof(sd_emmc_gpios);
    dev.metadata_list = sd_emmc_metadata;
    dev.metadata_count = countof(sd_emmc_metadata);
    dev.child_list = sd_emmc_children;
    dev.child_count = countof(sd_emmc_children);
    return dev;
}();

} // namespace

zx_status_t Sherlock::BCM43458LpoClockInit() {
    auto status = gpio_impl_.SetAltFunction(T931_WIFI_LPO_CLK, T931_WIFI_LPO_CLK_FN);
    if (status != ZX_OK) {
        return status;
    }

    zx::bti bti;
    status = iommu_.GetBti(BTI_BOARD, 0, &bti);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: GetBti() error: %d\n", __func__, status);
        return status;
    }

    std::optional<ddk::MmioBuffer> buf;
    zx::unowned_resource res(get_root_resource());
    status = ddk::MmioBuffer::Create(T931_PWM_EF_BASE, T931_PWM_LENGTH, *res,
                                     ZX_CACHE_POLICY_UNCACHED_DEVICE, &buf);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ddk::MmioBuffer::Create() error: %d\n", __func__, status);
        return status;
    }

    // Enable PWM_E to satisfy the 32.7KHz LPO clock source.
    // These values were taken from:
    //   linux/drivers/amlogic/pwm/pwm_meson.c
    buf->Write32(0x016d016e, T931_PWM_PWM_E);
    buf->Write32(0x016d016d, T931_PWM_E2);
    buf->Write32(0x0a0a0609, T931_PWM_TIME_EF);
    buf->Write32(0x02808003, T931_PWM_MISC_REG_EF);

    return ZX_OK;
}

zx_status_t Sherlock::SdioInit() {
    zx_status_t status;

    // Configure eMMC-SD soc pads.
    if (((status = gpio_impl_.SetAltFunction(T931_SDIO_D0, T931_SDIO_D0_FN)) != ZX_OK) ||
        ((status = gpio_impl_.SetAltFunction(T931_SDIO_D1, T931_SDIO_D1_FN)) != ZX_OK) ||
        ((status = gpio_impl_.SetAltFunction(T931_SDIO_D2, T931_SDIO_D2_FN)) != ZX_OK) ||
        ((status = gpio_impl_.SetAltFunction(T931_SDIO_D3, T931_SDIO_D3_FN)) != ZX_OK) ||
        ((status = gpio_impl_.SetAltFunction(T931_SDIO_CLK, T931_SDIO_CLK_FN)) != ZX_OK) ||
        ((status = gpio_impl_.SetAltFunction(T931_SDIO_CMD, T931_SDIO_CMD_FN)) != ZX_OK)) {
        return status;
    }

    status = gpio_impl_.SetAltFunction(T931_WIFI_REG_ON, T931_WIFI_REG_ON_FN);
    if (status != ZX_OK) {
        return status;
    }

    status = gpio_impl_.SetAltFunction(T931_WIFI_HOST_WAKE, T931_WIFI_HOST_WAKE_FN);
    if (status != ZX_OK) {
        return status;
    }

    status = pbus_.DeviceAdd(&sdio_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd() error: %d\n", __func__, status);
        return status;
    }
    return ZX_OK;
}

} // namespace sherlock
