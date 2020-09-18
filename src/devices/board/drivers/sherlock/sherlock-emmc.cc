// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/mmio/mmio.h>
#include <lib/zx/handle.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/gpt.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/sdmmc.h>
#include <hw/reg.h>
#include <soc/aml-common/aml-sdmmc.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

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

static aml_sdmmc_config_t sherlock_config = {
    .supports_dma = true,
    // As per AMlogic, on S912 chipset, HS400 mode can be operated at 125MHZ or low.
    .min_freq = 400'000,
    .max_freq = 120'000'000,
    .version_3 = true,
    .prefs = SDMMC_HOST_PREFS_DISABLE_HS400,
};

static aml_sdmmc_config_t luis_config = {
    .supports_dma = true,
    .min_freq = 400'000,
    .max_freq = 166'666'667,  // The expected eMMC clock frequency on Luis is 166 MHz.
    .version_3 = true,
    .prefs = SDMMC_HOST_PREFS_DISABLE_HS400,
};

static const guid_map_t guid_map[] = {
    {"boot", GUID_ZIRCON_A_VALUE},
    {"system", GUID_ZIRCON_B_VALUE},
    {"recovery", GUID_ZIRCON_R_VALUE},
    {"cache", GUID_FVM_VALUE},
};

static_assert(sizeof(guid_map) / sizeof(guid_map[0]) <= DEVICE_METADATA_GUID_MAP_MAX_ENTRIES);

static const pbus_metadata_t sherlock_emmc_metadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = &sherlock_config,
        .data_size = sizeof(sherlock_config),
    },
    {
        .type = DEVICE_METADATA_GUID_MAP,
        .data_buffer = guid_map,
        .data_size = sizeof(guid_map),
    },
};

static const pbus_metadata_t luis_emmc_metadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = &luis_config,
        .data_size = sizeof(luis_config),
    },
    {
        .type = DEVICE_METADATA_GUID_MAP,
        .data_buffer = guid_map,
        .data_size = sizeof(guid_map),
    },
};

static const pbus_boot_metadata_t emmc_boot_metadata[] = {
    {
        .zbi_type = DEVICE_METADATA_PARTITION_MAP,
        .zbi_extra = 0,
    },
};

static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, T931_EMMC_RST),
};
static const device_fragment_part_t gpio_fragment[] = {
    {countof(root_match), root_match},
    {countof(gpio_match), gpio_match},
};
static const device_fragment_t fragments[] = {
    {countof(gpio_fragment), gpio_fragment},
};

}  // namespace

zx_status_t Sherlock::EmmcInit() {
  // set alternate functions to enable EMMC
  gpio_impl_.SetAltFunction(T931_EMMC_D0, T931_EMMC_D0_FN);
  gpio_impl_.SetAltFunction(T931_EMMC_D1, T931_EMMC_D1_FN);
  gpio_impl_.SetAltFunction(T931_EMMC_D2, T931_EMMC_D2_FN);
  gpio_impl_.SetAltFunction(T931_EMMC_D3, T931_EMMC_D3_FN);
  gpio_impl_.SetAltFunction(T931_EMMC_D4, T931_EMMC_D4_FN);
  gpio_impl_.SetAltFunction(T931_EMMC_D5, T931_EMMC_D5_FN);
  gpio_impl_.SetAltFunction(T931_EMMC_D6, T931_EMMC_D6_FN);
  gpio_impl_.SetAltFunction(T931_EMMC_D7, T931_EMMC_D7_FN);
  gpio_impl_.SetAltFunction(T931_EMMC_CLK, T931_EMMC_CLK_FN);
  gpio_impl_.SetAltFunction(T931_EMMC_RST, T931_EMMC_RST_FN);
  gpio_impl_.SetAltFunction(T931_EMMC_CMD, T931_EMMC_CMD_FN);
  gpio_impl_.SetAltFunction(T931_EMMC_DS, T931_EMMC_DS_FN);

  pbus_dev_t emmc_dev = {};
  emmc_dev.name = "sherlock-emmc";
  emmc_dev.vid = PDEV_VID_AMLOGIC;
  emmc_dev.pid = PDEV_PID_GENERIC;
  emmc_dev.did = PDEV_DID_AMLOGIC_SDMMC_C;
  emmc_dev.mmio_list = emmc_mmios;
  emmc_dev.mmio_count = countof(emmc_mmios);
  emmc_dev.irq_list = emmc_irqs;
  emmc_dev.irq_count = countof(emmc_irqs);
  emmc_dev.bti_list = emmc_btis;
  emmc_dev.bti_count = countof(emmc_btis);
  emmc_dev.metadata_list = sherlock_emmc_metadata;
  emmc_dev.metadata_count = countof(sherlock_emmc_metadata);
  emmc_dev.boot_metadata_list = emmc_boot_metadata;
  emmc_dev.boot_metadata_count = countof(emmc_boot_metadata);

  if (pid_ == PDEV_PID_LUIS) {
    emmc_dev.metadata_list = luis_emmc_metadata;
    emmc_dev.metadata_count = countof(luis_emmc_metadata);
  }

  zx_status_t status = pbus_.CompositeDeviceAdd(&emmc_dev, fragments, countof(fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace sherlock
