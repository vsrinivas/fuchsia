// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <hw/reg.h>
#include <lib/mmio/mmio.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-common/aml-sd-emmc.h>
#include <wifi/wifi-config.h>

#include "astro.h"

namespace astro {

static const pbus_boot_metadata_t wifi_boot_metadata[] = {
    {
        .zbi_type = DEVICE_METADATA_MAC_ADDRESS,
        .zbi_extra = MACADDR_WIFI,
    },
};

static const pbus_mmio_t sd_emmc_mmios[] = {
    {
        .base = S905D2_EMMC_B_SDIO_BASE,
        .length = S905D2_EMMC_B_SDIO_LENGTH,
    },
    {
        .base = S905D2_GPIO_BASE,
        .length = S905D2_GPIO_LENGTH,
    },
    {
        .base = S905D2_HIU_BASE,
        .length = S905D2_HIU_LENGTH,
    },
};

static const pbus_irq_t sd_emmc_irqs[] = {
    {
        .irq = S905D2_EMMC_B_SDIO_IRQ,
        .mode = 0,
    },
};

static const pbus_bti_t sd_emmc_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_SDIO,
    },
};

static const pbus_gpio_t sd_emmc_gpios[] = {
    {
        .gpio = S905D2_GPIOX(6),
    },
};

static aml_sd_emmc_config_t config = {
    .supports_dma = true,
    .min_freq = 400000,
    .max_freq = 50000000,
};

static const wifi_config_t wifi_config = {
    .oob_irq_mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
};

static const pbus_metadata_t sd_emmc_metadata[] = {
    {
        .type        = DEVICE_METADATA_EMMC_CONFIG,
        .data_buffer = &config,
        .data_size   = sizeof(config),
    },
    {
        .type        = DEVICE_METADATA_WIFI_CONFIG,
        .data_buffer = &wifi_config,
        .data_size   = sizeof(wifi_config),
    },
};

static const pbus_dev_t sd_emmc_dev = []() {
    pbus_dev_t dev = {};
    dev.name = "aml-sdio";
    dev.vid = PDEV_VID_AMLOGIC;
    dev.pid = PDEV_PID_GENERIC;
    dev.did = PDEV_DID_AMLOGIC_SD_EMMC_B;
    dev.mmio_list = sd_emmc_mmios;
    dev.mmio_count = countof(sd_emmc_mmios);
    dev.irq_list = sd_emmc_irqs;
    dev.irq_count = countof(sd_emmc_irqs);
    dev.bti_list = sd_emmc_btis;
    dev.bti_count = countof(sd_emmc_btis);
    dev.gpio_list = sd_emmc_gpios;
    dev.gpio_count = countof(sd_emmc_gpios);
    dev.metadata_list = sd_emmc_metadata;
    dev.metadata_count = countof(sd_emmc_metadata);
    dev.boot_metadata_list = wifi_boot_metadata;
    dev.boot_metadata_count = countof(wifi_boot_metadata);
    return dev;
}();

// Composite binding rules for wifi driver.
static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t sdio_fn1_match[]  = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_SDIO),
    BI_ABORT_IF(NE, BIND_SDIO_VID, 0x02d0),
    BI_ABORT_IF(NE, BIND_SDIO_FUNCTION, 1),
    BI_MATCH_IF(EQ, BIND_SDIO_PID, 0x4345),
    BI_MATCH_IF(EQ, BIND_SDIO_PID, 0x4359),
};
static const zx_bind_inst_t sdio_fn2_match[]  = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_SDIO),
    BI_ABORT_IF(NE, BIND_SDIO_VID, 0x02d0),
    BI_ABORT_IF(NE, BIND_SDIO_FUNCTION, 2),
    BI_MATCH_IF(EQ, BIND_SDIO_PID, 0x4345),
    BI_MATCH_IF(EQ, BIND_SDIO_PID, 0x4359),
};
static const zx_bind_inst_t oob_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, S905D2_WIFI_SDIO_WAKE_HOST),
};
static const device_component_part_t sdio_fn1_component[] = {
    { countof(root_match), root_match },
    { countof(sdio_fn1_match), sdio_fn1_match },
};
static const device_component_part_t sdio_fn2_component[] = {
    { countof(root_match), root_match },
    { countof(sdio_fn2_match), sdio_fn2_match },
};
static const device_component_part_t oob_gpio_component[] = {
    { countof(root_match), root_match },
    { countof(oob_gpio_match), oob_gpio_match },
};
static const device_component_t wifi_composite[] = {
    { countof(sdio_fn1_component), sdio_fn1_component },
    { countof(sdio_fn2_component), sdio_fn2_component },
    { countof(oob_gpio_component), oob_gpio_component },
};

zx_status_t Astro::SdEmmcConfigurePortB() {
    // Clear GPIO_X
    gpio_impl_.SetAltFunction(S905D2_WIFI_SDIO_D0, 0);
    gpio_impl_.SetAltFunction(S905D2_WIFI_SDIO_D1, 0);
    gpio_impl_.SetAltFunction(S905D2_WIFI_SDIO_D2, 0);
    gpio_impl_.SetAltFunction(S905D2_WIFI_SDIO_D3, 0);
    gpio_impl_.SetAltFunction(S905D2_WIFI_SDIO_CLK, 0);
    gpio_impl_.SetAltFunction(S905D2_WIFI_SDIO_CMD, 0);
    gpio_impl_.SetAltFunction(S905D2_WIFI_SDIO_WAKE_HOST, 0);
    // Clear GPIO_C
    gpio_impl_.SetAltFunction(S905D2_GPIOC(0), 0);
    gpio_impl_.SetAltFunction(S905D2_GPIOC(1), 0);
    gpio_impl_.SetAltFunction(S905D2_GPIOC(2), 0);
    gpio_impl_.SetAltFunction(S905D2_GPIOC(3), 0);
    gpio_impl_.SetAltFunction(S905D2_GPIOC(4), 0);
    gpio_impl_.SetAltFunction(S905D2_GPIOC(5), 0);

    zx_status_t status;
    std::optional<ddk::MmioBuffer> gpio_base;
    // Please do not use get_root_resource() in new code. See ZX-1467.
    zx::unowned_resource resource(get_root_resource());

    const uint64_t aligned_gpio_base = ROUNDDOWN(S905D2_GPIO_BASE, PAGE_SIZE);
    size_t aligned_size = ROUNDUP((S905D2_GPIO_BASE - aligned_gpio_base) +
                                   S905D2_GPIO_LENGTH,
                                   PAGE_SIZE);

    status = ddk::MmioBuffer::Create(aligned_gpio_base, aligned_size, *resource,
                                     ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                     &gpio_base);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Create(gpio) error: %d\n",
               __func__, status);
    }

    //TODO(ravoorir): Figure out if we need gpio protocol ops to modify these
    //gpio registers.
    const uint32_t actual_base_offset = S905D2_GPIO_BASE - aligned_gpio_base;
    uint32_t preg_pad_gpio5_val =
        gpio_base->Read32(actual_base_offset + (S905D2_PREG_PAD_GPIO5_O << 2)) |
        AML_SDIO_PORTB_GPIO_REG_5_VAL;
    gpio_base->Write32(preg_pad_gpio5_val,
                       actual_base_offset + (S905D2_PREG_PAD_GPIO5_O << 2));

    uint32_t periphs_pin_mux2_val =
        gpio_base->Read32(
            actual_base_offset + (S905D2_PERIPHS_PIN_MUX_2 << 2)) |
            AML_SDIO_PORTB_PERIPHS_PINMUX2_VAL;
    gpio_base->Write32(periphs_pin_mux2_val,
                       actual_base_offset + (S905D2_PERIPHS_PIN_MUX_2 << 2));

    uint32_t gpio2_en_n_val =
        gpio_base->Read32(
            actual_base_offset + (S905D2_PREG_PAD_GPIO2_EN_N << 2)) &
            AML_SDIO_PORTB_PERIPHS_GPIO2_EN;
    gpio_base->Write32(gpio2_en_n_val,
                       actual_base_offset + (S905D2_PREG_PAD_GPIO2_EN_N << 2));

    // Configure clock settings
    std::optional<ddk::MmioBuffer> hiu_base;
    status = ddk::MmioBuffer::Create(S905D2_HIU_BASE, S905D2_HIU_LENGTH,
                                     *resource,
                                     ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                     &hiu_base);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Create(hiu) error: %d\n",
               __func__, status);
    }

    uint32_t hhi_gclock_val = hiu_base->Read32(HHI_GCLK_MPEG0_OFFSET << 2) |
                              AML_SDIO_PORTB_HHI_GCLK_MPEG0_VAL;
    hiu_base->Write32(hhi_gclock_val, HHI_GCLK_MPEG0_OFFSET << 2);

    uint32_t hh1_sd_emmc_clock_val =
        hiu_base->Read32(HHI_SD_EMMC_CLK_CNTL_OFFSET << 2) &
        AML_SDIO_PORTB_SD_EMMC_CLK_VAL;
    hiu_base->Write32(hh1_sd_emmc_clock_val, HHI_SD_EMMC_CLK_CNTL_OFFSET << 2);

    return status;
}

zx_status_t Astro::SdioInit() {
    zx_status_t status;

    SdEmmcConfigurePortB();

    if ((status = pbus_.DeviceAdd(&sd_emmc_dev)) != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd sd_emmc failed: %d\n",
               __func__, status);
        return status;
    }

    // Add a composite device for wifi driver.
    const zx_device_prop_t props[] = {
        { BIND_PLATFORM_DEV_VID, 0, PDEV_VID_BROADCOM },
        { BIND_PLATFORM_DEV_PID, 0, PDEV_PID_BCM43458 },
        { BIND_PLATFORM_DEV_DID, 0, PDEV_DID_BCM_WIFI },
    };

    status = DdkAddComposite("wifi", props, countof(props), wifi_composite,
                             countof(wifi_composite), 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd_composite failed: %d\n",
               __func__, status);
        return status;
    }

    return ZX_OK;
}

} // namespace astro
