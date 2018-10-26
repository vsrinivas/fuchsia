// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/gpt.h>
#include <ddktl/mmio.h>
#include <ddk/platform-defs.h>
#include <fbl/optional.h>
#include <hw/reg.h>
#include <lib/zx/handle.h>

#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>
#include <soc/aml-common/aml-sd-emmc.h>

#include "sherlock.h"

namespace sherlock {

namespace {

constexpr pbus_mmio_t emmc_mmios[] = {
    {
        .base = T931_SD_EMMC_C_BASE,
        .length = T931_SD_EMMC_C_LENGTH,
    },
};

constexpr pbus_irq_t emmc_irqs[] = {
    {
        .irq = T931_SD_EMMC_C_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

constexpr pbus_bti_t emmc_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_EMMC,
    },
};

static const pbus_gpio_t emmc_gpios[] = {
    {
        .gpio = T931_EMMC_RST,
    },
};

static aml_sd_emmc_config_t config = {
    .supports_dma = true,
    //As per AMlogic, on S912 chipset, HS400 mode can be operated at 125MHZ or low.
    .min_freq = 400000,
    .max_freq = 120000000,
};

static const guid_map_t guid_map[] = {
    { "boot", GUID_ZIRCON_A_VALUE },
    { "recovery", GUID_ZIRCON_R_VALUE },
    { "cache", GUID_FVM_VALUE }
};
static_assert(sizeof(guid_map) / sizeof(guid_map[0]) <= DEVICE_METADATA_GUID_MAP_MAX_ENTRIES);

static const pbus_metadata_t emmc_metadata[] = {
    {
        .type       = DEVICE_METADATA_PRIVATE,
        .data_buffer       = &config,
        .data_size        = sizeof(config),
    },
    {
        .type = DEVICE_METADATA_GUID_MAP,
        .data_buffer = guid_map,
        .data_size = sizeof(guid_map),
    }
};

static const pbus_boot_metadata_t emmc_boot_metadata[] = {
    {
        .zbi_type = DEVICE_METADATA_PARTITION_MAP,
        .zbi_extra = 0,
    },
};

static pbus_dev_t emmc_dev = [](){
    pbus_dev_t dev;
    dev.name = "sherlock-emmc";
    dev.vid = PDEV_VID_AMLOGIC;
    dev.pid = PDEV_PID_GENERIC;
    dev.did = PDEV_DID_AMLOGIC_SD_EMMC;
    dev.mmio_list = emmc_mmios;
    dev.mmio_count = countof(emmc_mmios);
    dev.irq_list = emmc_irqs;
    dev.irq_count = countof(emmc_irqs);
    dev.bti_list = emmc_btis;
    dev.bti_count = countof(emmc_btis);
    dev.gpio_list = emmc_gpios;
    dev.gpio_count = countof(emmc_gpios);
    dev.metadata_list = emmc_metadata;
    dev.metadata_count = countof(emmc_metadata);
    dev.boot_metadata_list = emmc_boot_metadata;
    dev.boot_metadata_count = countof(emmc_boot_metadata);
    return dev;
}();

} // namespace

zx_status_t Sherlock::EmmcInit() {
    // set alternate functions to enable EMMC
    gpio_impl_set_alt_function(&gpio_impl_, T931_EMMC_D0, T931_EMMC_D0_FN);
    gpio_impl_set_alt_function(&gpio_impl_, T931_EMMC_D1, T931_EMMC_D1_FN);
    gpio_impl_set_alt_function(&gpio_impl_, T931_EMMC_D2, T931_EMMC_D2_FN);
    gpio_impl_set_alt_function(&gpio_impl_, T931_EMMC_D3, T931_EMMC_D3_FN);
    gpio_impl_set_alt_function(&gpio_impl_, T931_EMMC_D4, T931_EMMC_D4_FN);
    gpio_impl_set_alt_function(&gpio_impl_, T931_EMMC_D5, T931_EMMC_D5_FN);
    gpio_impl_set_alt_function(&gpio_impl_, T931_EMMC_D6, T931_EMMC_D6_FN);
    gpio_impl_set_alt_function(&gpio_impl_, T931_EMMC_D7, T931_EMMC_D7_FN);
    gpio_impl_set_alt_function(&gpio_impl_, T931_EMMC_CLK, T931_EMMC_CLK_FN);
    gpio_impl_set_alt_function(&gpio_impl_, T931_EMMC_RST, T931_EMMC_RST_FN);
    gpio_impl_set_alt_function(&gpio_impl_, T931_EMMC_CMD, T931_EMMC_CMD_FN);
    gpio_impl_set_alt_function(&gpio_impl_, T931_EMMC_DS, T931_EMMC_DS_FN);

    auto status = pbus_.DeviceAdd(&emmc_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd failed %d\n", __func__, status);
        return status;
    }

    return ZX_OK;
}

} // namespace sherlock
