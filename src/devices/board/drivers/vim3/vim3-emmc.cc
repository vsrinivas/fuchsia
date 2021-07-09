// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/sdmmc/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/gpt.h>
#include <soc/aml-a311d/a311d-gpio.h>
#include <soc/aml-a311d/a311d-hw.h>
#include <soc/aml-common/aml-sdmmc.h>

#include "vim3.h"

namespace vim3 {
#define BIT_MASK(start, count) (((1 << (count)) - 1) << (start))
#define SET_BITS(dest, start, count, value) \
  ((dest & ~BIT_MASK(start, count)) | (((value) << (start)) & BIT_MASK(start, count)))

static const pbus_mmio_t emmc_mmios[] = {{
    .base = A311D_EMMC_C_BASE,
    .length = A311D_EMMC_C_LENGTH,
}};

static const pbus_irq_t emmc_irqs[] = {
    {
        .irq = A311D_SD_EMMC_C_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_bti_t emmc_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_EMMC,
    },
};

static aml_sdmmc_config_t config = {
    .supports_dma = true,
    .min_freq = 400000,
    .max_freq = 120000000,
    .version_3 = true,
    .prefs = SDMMC_HOST_PREFS_DISABLE_HS400,
};

static const pbus_metadata_t emmc_metadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = reinterpret_cast<const uint8_t*>(&config),
        .data_size = sizeof(config),
    },
};

static const pbus_boot_metadata_t emmc_boot_metadata[] = {
    {
        .zbi_type = DEVICE_METADATA_PARTITION_MAP,
        .zbi_extra = 0,
    },
};

static const zx_bind_inst_t gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, A311D_GPIOBOOT(12)),
};
static const device_fragment_part_t gpio_fragment[] = {
    {countof(gpio_match), gpio_match},
};
static const device_fragment_t fragments[] = {
    {"gpio", countof(gpio_fragment), gpio_fragment},
};

zx_status_t Vim3::EmmcInit() {
  zx_status_t status;

  pbus_dev_t emmc_dev = {};
  emmc_dev.name = "aml_emmc";
  emmc_dev.vid = PDEV_VID_AMLOGIC;
  emmc_dev.pid = PDEV_PID_GENERIC;
  emmc_dev.did = PDEV_DID_AMLOGIC_SDMMC_C;
  emmc_dev.mmio_list = emmc_mmios;
  emmc_dev.mmio_count = countof(emmc_mmios);
  emmc_dev.irq_list = emmc_irqs;
  emmc_dev.irq_count = countof(emmc_irqs);
  emmc_dev.bti_list = emmc_btis;
  emmc_dev.bti_count = countof(emmc_btis);
  emmc_dev.metadata_list = emmc_metadata;
  emmc_dev.metadata_count = countof(emmc_metadata);
  emmc_dev.boot_metadata_list = emmc_boot_metadata;
  emmc_dev.boot_metadata_count = countof(emmc_boot_metadata);

  // set alternate functions to enable EMMC
  gpio_impl_.SetAltFunction(A311D_GPIOBOOT(0), A311D_GPIOBOOT_0_EMMC_D0_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOBOOT(1), A311D_GPIOBOOT_1_EMMC_D1_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOBOOT(2), A311D_GPIOBOOT_2_EMMC_D2_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOBOOT(3), A311D_GPIOBOOT_3_EMMC_D3_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOBOOT(4), A311D_GPIOBOOT_4_EMMC_D4_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOBOOT(5), A311D_GPIOBOOT_5_EMMC_D5_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOBOOT(6), A311D_GPIOBOOT_6_EMMC_D6_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOBOOT(7), A311D_GPIOBOOT_7_EMMC_D7_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOBOOT(8), A311D_GPIOBOOT_8_EMMC_CLK_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOBOOT(10), A311D_GPIOBOOT_10_EMMC_CMD_FN);
  // gpio_impl_.SetAltFunction(A311D_GPIOBOOT(12), 1);
  gpio_impl_.SetAltFunction(A311D_GPIOBOOT(13), A311D_GPIOBOOT_13_EMMC_DS_FN);

  gpio_impl_.ConfigOut(A311D_GPIOBOOT(14), 1);

  status = pbus_.CompositeDeviceAdd(&emmc_dev, reinterpret_cast<uint64_t>(fragments),
                                    countof(fragments), nullptr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "SdEmmcInit could not add emmc_dev: %d\n", status);
    return status;
  }

  return ZX_OK;
}
}  // namespace vim3
