// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/mmio-buffer.h>
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
        .type        = DEVICE_METADATA_PRIVATE,
        .data_buffer = &wifi_config,
        .data_size   = sizeof(wifi_config),
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
        .gpio_list = wifi_gpios,
        .gpio_count = countof(wifi_gpios),
        .metadata_list = wifi_metadata,
        .metadata_count = countof(wifi_metadata),
        .boot_metadata_list = wifi_boot_metadata,
        .boot_metadata_count = countof(wifi_boot_metadata),
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

static const pbus_irq_t aml_sd_emmc_irqs[] = {
    {
        .irq = S905D2_EMMC_B_SDIO_IRQ,
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
    .supports_dma = true,
    .min_freq = 400000,
    .max_freq = 50000000,
};

static const pbus_metadata_t aml_sd_emmc_metadata[] = {
    {
        .type        = DEVICE_METADATA_PRIVATE,
        .data_buffer = &config,
        .data_size   = sizeof(config),
    }
};

static const pbus_dev_t aml_sd_emmc_dev = {
    .name = "aml-sdio",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_AMLOGIC_SD_EMMC_B,
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

static zx_status_t aml_sd_emmc_configure_portB(aml_bus_t* bus) {
    // Clear GPIO_X
    gpio_impl_set_alt_function(&bus->gpio, S905D2_WIFI_SDIO_D0, 0);
    gpio_impl_set_alt_function(&bus->gpio, S905D2_WIFI_SDIO_D1, 0);
    gpio_impl_set_alt_function(&bus->gpio, S905D2_WIFI_SDIO_D2, 0);
    gpio_impl_set_alt_function(&bus->gpio, S905D2_WIFI_SDIO_D3, 0);
    gpio_impl_set_alt_function(&bus->gpio, S905D2_WIFI_SDIO_CLK, 0);
    gpio_impl_set_alt_function(&bus->gpio, S905D2_WIFI_SDIO_CMD, 0);
    gpio_impl_set_alt_function(&bus->gpio, S905D2_WIFI_SDIO_WAKE_HOST, 0);
    // Clear GPIO_C
    gpio_impl_set_alt_function(&bus->gpio, S905D2_GPIOC(0), 0);
    gpio_impl_set_alt_function(&bus->gpio, S905D2_GPIOC(1), 0);
    gpio_impl_set_alt_function(&bus->gpio, S905D2_GPIOC(2), 0);
    gpio_impl_set_alt_function(&bus->gpio, S905D2_GPIOC(3), 0);
    gpio_impl_set_alt_function(&bus->gpio, S905D2_GPIOC(4), 0);
    gpio_impl_set_alt_function(&bus->gpio, S905D2_GPIOC(5), 0);

    zx_status_t status;
    mmio_buffer_t gpio_base, hiu_base;
    zx_handle_t resource = get_root_resource();

    const uint64_t aligned_gpio_base = ROUNDDOWN(S905D2_GPIO_BASE, PAGE_SIZE);
    size_t aligned_size = ROUNDUP((S905D2_GPIO_BASE - aligned_gpio_base) + S905D2_GPIO_LENGTH,
                                  PAGE_SIZE);

    status = mmio_buffer_init_physical(&gpio_base, aligned_gpio_base, aligned_size,
                                       resource, ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_sd_emmc_configure_portB: mmio_buffer_init failed %d\n", status);
        return status;
    }
    uintptr_t gpio_vaddr = (uintptr_t)gpio_base.vaddr + S905D2_GPIO_BASE - aligned_gpio_base;

    //TODO(ravoorir): Figure out if we need gpio protocol ops to modify these
    //gpio registers.
    uintptr_t preg_pad_gpio5 = gpio_vaddr + (S905D2_PREG_PAD_GPIO5_O << 2);
    uint32_t preg_pad_gpio5_val = readl((void*)(preg_pad_gpio5)) | AML_SDIO_PORTB_GPIO_REG_5_VAL;
    writel(preg_pad_gpio5_val, (void *)(preg_pad_gpio5));

    uintptr_t periphs_pin_mux2 = gpio_vaddr + (S905D2_PERIPHS_PIN_MUX_2 << 2);
    uint32_t periphs_pin_mux2_val = readl((void*)(periphs_pin_mux2)) |
                                            AML_SDIO_PORTB_PERIPHS_PINMUX2_VAL;
    writel(periphs_pin_mux2_val, (void *)(periphs_pin_mux2));

    uintptr_t gpio2_en_n = gpio_vaddr + (S905D2_PREG_PAD_GPIO2_EN_N << 2);
    uint32_t gpio2_en_n_val = readl((void*)(gpio2_en_n)) & AML_SDIO_PORTB_PERIPHS_GPIO2_EN;
    writel(gpio2_en_n_val, (void *)(gpio2_en_n));

    // Configure clock settings
    status = mmio_buffer_init_physical(&hiu_base, S905D2_HIU_BASE, S905D2_HIU_LENGTH,
                                       resource,
                                       ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_sd_emmc_configure_portB: mmio_buffer_init failed %d\n", status);
        mmio_buffer_release(&gpio_base);
        return status;
    }
    uintptr_t hiu_vaddr = (uintptr_t)hiu_base.vaddr;

    uintptr_t hhi_gclock = hiu_vaddr + (HHI_GCLK_MPEG0_OFFSET << 2);
    uint32_t hhi_gclock_val = readl((void*)(hhi_gclock)) | AML_SDIO_PORTB_HHI_GCLK_MPEG0_VAL;
    writel(hhi_gclock_val, (void *)(hhi_gclock));


    uintptr_t hhi_sd_emmc_clock = hiu_vaddr + (HHI_SD_EMMC_CLK_CNTL_OFFSET << 2);
    uint32_t hhi_sd_emmc_clock_val = readl((void*)(hhi_sd_emmc_clock)) &
                                                          AML_SDIO_PORTB_SD_EMMC_CLK_VAL;
    writel(hhi_sd_emmc_clock_val, (void *)(hhi_sd_emmc_clock));

    mmio_buffer_release(&gpio_base);
    mmio_buffer_release(&hiu_base);

    return status;
}

zx_status_t aml_sdio_init(aml_bus_t* bus) {
    zx_status_t status;

    aml_sd_emmc_configure_portB(bus);

    if ((status = pbus_device_add(&bus->pbus, &aml_sd_emmc_dev)) != ZX_OK) {
        zxlogf(ERROR, "aml_sdio_init could not add aml_sd_emmc_dev: %d\n", status);
        return status;
    }

    return ZX_OK;
}
