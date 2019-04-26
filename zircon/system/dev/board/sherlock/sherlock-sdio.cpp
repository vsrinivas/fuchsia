// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <lib/mmio/mmio.h>
#include <ddktl/protocol/gpioimpl.h>
#include <fbl/algorithm.h>
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


constexpr pbus_boot_metadata_t wifi_boot_metadata[] = {
    {
        .zbi_type = DEVICE_METADATA_MAC_ADDRESS,
        .zbi_extra = MACADDR_WIFI,
    },
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

constexpr wifi_config_t wifi_config = {
    .oob_irq_mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
};

constexpr pbus_metadata_t sd_emmc_metadata[] = {
    {
        .type = DEVICE_METADATA_EMMC_CONFIG,
        .data_buffer = &sd_emmc_config,
        .data_size = sizeof(sd_emmc_config),
    },
    {
        .type = DEVICE_METADATA_WIFI_CONFIG,
        .data_buffer = &wifi_config,
        .data_size = sizeof(wifi_config),
    },
};

const pbus_dev_t sdio_dev = []() {
    pbus_dev_t dev;
    dev.name = "sherlock-sd-emmc";
    dev.vid = PDEV_VID_AMLOGIC;
    dev.pid = PDEV_PID_GENERIC;
    dev.did = PDEV_DID_AMLOGIC_SD_EMMC_A;
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
    dev.boot_metadata_list = wifi_boot_metadata;
    dev.boot_metadata_count = countof(wifi_boot_metadata);
    return dev;
}();

// Composite binding rules for wifi driver.
constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
constexpr zx_bind_inst_t sdio_match[]  = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_SDIO),
    BI_ABORT_IF(NE, BIND_SDIO_VID, 0x02d0),
    // The specific function number doesn't matter as long as we bind to one and only one of the
    // created SDIO devices. The numbers start at 1, so just bind to the first device.
    BI_ABORT_IF(NE, BIND_SDIO_FUNCTION, 1),
    BI_MATCH_IF(EQ, BIND_SDIO_PID, 0x4345),
    BI_MATCH_IF(EQ, BIND_SDIO_PID, 0x4359),
};
constexpr zx_bind_inst_t oob_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, T931_WIFI_HOST_WAKE),
};
constexpr device_component_part_t sdio_component[] = {
    { fbl::count_of(root_match), root_match },
    { fbl::count_of(sdio_match), sdio_match },
};
constexpr device_component_part_t oob_gpio_component[] = {
    { fbl::count_of(root_match), root_match },
    { fbl::count_of(oob_gpio_match), oob_gpio_match },
};
constexpr device_component_t wifi_composite[] = {
    { fbl::count_of(sdio_component), sdio_component },
    { fbl::count_of(oob_gpio_component), oob_gpio_component },
};

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
    // Please do not use get_root_resource() in new code. See ZX-1497.
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

    // Add a composite device for wifi driver.
    constexpr zx_device_prop_t props[] = {
        { BIND_PLATFORM_DEV_VID, 0, PDEV_VID_BROADCOM },
        { BIND_PLATFORM_DEV_PID, 0, PDEV_PID_BCM43458 },
        { BIND_PLATFORM_DEV_DID, 0, PDEV_DID_BCM_WIFI },
    };

    status = DdkAddComposite("wifi", props, fbl::count_of(props), wifi_composite,
                             fbl::count_of(wifi_composite), 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: device_add_composite failed: %d\n", __func__, status);
        return status;
    }

    return ZX_OK;
}

} // namespace sherlock
