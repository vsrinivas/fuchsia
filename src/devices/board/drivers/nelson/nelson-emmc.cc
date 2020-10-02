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
#include <soc/aml-s905d3/s905d3-gpio.h>
#include <soc/aml-s905d3/s905d3-hw.h>

#include "nelson-gpios.h"
#include "nelson.h"

namespace nelson {

namespace {

constexpr pbus_mmio_t emmc_mmios[] = {
    {
        .base = S905D3_EMMC_C_SDIO_BASE,
        .length = S905D3_EMMC_C_SDIO_LENGTH,
    },
};

constexpr pbus_irq_t emmc_irqs[] = {
    {
        .irq = S905D3_EMMC_C_SDIO_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

constexpr pbus_bti_t emmc_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_EMMC,
    },
};

static aml_sdmmc_config_t config = {
    .supports_dma = true,
    // As per AMlogic, on S912 chipset, HS400 mode can be operated at 125MHZ or low.
    .min_freq = 400000,
    .max_freq = 120000000,
    .version_3 = true,
    .prefs = SDMMC_HOST_PREFS_DISABLE_HS400,
};

// TODO(fxbug.dev/43729): Choose better partitions later
static const guid_map_t guid_map[] = {
    {"system_a", GUID_ZIRCON_R_VALUE},
    {"system_b", GUID_ZIRCON_B_VALUE},
    {"boot_a", GUID_ZIRCON_A_VALUE},
    {"data", GUID_FVM_VALUE},
};

static_assert(sizeof(guid_map) / sizeof(guid_map[0]) <= DEVICE_METADATA_GUID_MAP_MAX_ENTRIES);

static const pbus_metadata_t emmc_metadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = &config,
        .data_size = sizeof(config),
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

static pbus_dev_t emmc_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "nelson-emmc";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_AMLOGIC_SDMMC_C;
  dev.mmio_list = emmc_mmios;
  dev.mmio_count = countof(emmc_mmios);
  dev.irq_list = emmc_irqs;
  dev.irq_count = countof(emmc_irqs);
  dev.bti_list = emmc_btis;
  dev.bti_count = countof(emmc_btis);
  dev.metadata_list = emmc_metadata;
  dev.metadata_count = countof(emmc_metadata);
  dev.boot_metadata_list = emmc_boot_metadata;
  dev.boot_metadata_count = countof(emmc_boot_metadata);
  return dev;
}();

static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_EMMC_RESET),
};
static const device_fragment_part_t gpio_fragment[] = {
    {countof(root_match), root_match},
    {countof(gpio_match), gpio_match},
};
static const device_fragment_new_t fragments[] = {
    {"gpio", countof(gpio_fragment), gpio_fragment},
};

}  // namespace

zx_status_t Nelson::EmmcInit() {
  // set alternate functions to enable EMMC
  gpio_impl_.SetAltFunction(S905D3_EMMC_D0, S905D3_EMMC_D0_FN);
  gpio_impl_.SetAltFunction(S905D3_EMMC_D1, S905D3_EMMC_D1_FN);
  gpio_impl_.SetAltFunction(S905D3_EMMC_D2, S905D3_EMMC_D2_FN);
  gpio_impl_.SetAltFunction(S905D3_EMMC_D3, S905D3_EMMC_D3_FN);
  gpio_impl_.SetAltFunction(S905D3_EMMC_D4, S905D3_EMMC_D4_FN);
  gpio_impl_.SetAltFunction(S905D3_EMMC_D5, S905D3_EMMC_D5_FN);
  gpio_impl_.SetAltFunction(S905D3_EMMC_D6, S905D3_EMMC_D6_FN);
  gpio_impl_.SetAltFunction(S905D3_EMMC_D7, S905D3_EMMC_D7_FN);
  gpio_impl_.SetAltFunction(S905D3_EMMC_CLK, S905D3_EMMC_CLK_FN);
  gpio_impl_.SetAltFunction(S905D3_EMMC_RST, S905D3_EMMC_RST_FN);
  gpio_impl_.SetAltFunction(S905D3_EMMC_CMD, S905D3_EMMC_CMD_FN);
  gpio_impl_.SetAltFunction(S905D3_EMMC_DS, S905D3_EMMC_DS_FN);

  auto status = pbus_.CompositeDeviceAddNew(&emmc_dev, fragments, countof(fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAddNew failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace nelson
