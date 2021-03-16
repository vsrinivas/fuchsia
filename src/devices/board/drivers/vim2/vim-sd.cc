// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/platform-defs.h>
#include <lib/ddk/hw/reg.h>

#include <lib/ddk/metadata.h>
#include <soc/aml-a113/a113-hw.h>
#include <soc/aml-common/aml-sdmmc.h>
#include <soc/aml-s912/s912-gpio.h>
#include <soc/aml-s912/s912-hw.h>

#include "vim.h"

namespace vim {

static const pbus_mmio_t sd_mmios[] = {{
    .base = S912_SD_EMMC_B_BASE,
    .length = S912_SD_EMMC_B_LENGTH,
}};

static const pbus_irq_t sd_irqs[] = {
    {.irq = S912_SD_EMMC_B_IRQ, .mode = 0},
};

static const pbus_bti_t sd_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_SD,
    },
};

static aml_sdmmc_config_t config = {
    .supports_dma = true,
    .min_freq = 400000,
    .max_freq = 120000000,
    .version_3 = false,
    .prefs = 0,
};

static const pbus_metadata_t sd_metadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = &config,
        .data_size = sizeof(config),
    },
};

zx_status_t Vim::SdInit() {
  zx_status_t status;

  pbus_dev_t sd_dev = {};
  sd_dev.name = "aml_sd";
  sd_dev.vid = PDEV_VID_AMLOGIC;
  sd_dev.pid = PDEV_PID_GENERIC;
  sd_dev.did = PDEV_DID_AMLOGIC_SDMMC_B;
  sd_dev.mmio_list = sd_mmios;
  sd_dev.mmio_count = countof(sd_mmios);
  sd_dev.irq_list = sd_irqs;
  sd_dev.irq_count = countof(sd_irqs);
  sd_dev.bti_list = sd_btis;
  sd_dev.bti_count = countof(sd_btis);
  sd_dev.metadata_list = sd_metadata;
  sd_dev.metadata_count = countof(sd_metadata);

  // set alternate functions to enable SD
  gpio_impl_.SetAltFunction(S912_SDCARD_D0, S912_SDCARD_D0_FN);
  gpio_impl_.SetAltFunction(S912_SDCARD_D1, S912_SDCARD_D1_FN);
  gpio_impl_.SetAltFunction(S912_SDCARD_D2, S912_SDCARD_D2_FN);
  gpio_impl_.SetAltFunction(S912_SDCARD_D3, S912_SDCARD_D3_FN);
  gpio_impl_.SetAltFunction(S912_SDCARD_CLK, S912_SDCARD_CLK_FN);
  gpio_impl_.SetAltFunction(S912_SDCARD_CMD, S912_SDCARD_CMD_FN);

  if ((status = pbus_.DeviceAdd(&sd_dev)) != ZX_OK) {
    zxlogf(ERROR, "SdInit could not add sd_dev: %d", status);
    return status;
  }

  return ZX_OK;
}
}  // namespace vim
