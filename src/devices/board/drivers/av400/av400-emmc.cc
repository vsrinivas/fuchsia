// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/sdmmc/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/gpt.h>
#include <soc/aml-a5/a5-gpio.h>
#include <soc/aml-a5/a5-hw.h>
#include <soc/aml-common/aml-sdmmc.h>

#include "src/devices/board/drivers/av400/av400-emmc-bind.h"
#include "src/devices/board/drivers/av400/av400.h"

namespace av400 {
static const pbus_mmio_t emmc_mmios[] = {{
    .base = A5_EMMC_C_BASE,
    .length = A5_EMMC_C_LENGTH,
}};

static const pbus_irq_t emmc_irqs[] = {
    {
        .irq = A5_SD_EMMC_C_IRQ,
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
    .min_freq = 400'000,
    .max_freq = 200'000'000,
    .version_3 = true,
    .prefs = SDMMC_HOST_PREFS_DISABLE_HS400,
    .use_new_tuning = true,
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

zx_status_t Av400::EmmcInit() {
  zx_status_t status;

  pbus_dev_t emmc_dev = {};
  emmc_dev.name = "aml_emmc";
  emmc_dev.vid = PDEV_VID_AMLOGIC;
  emmc_dev.pid = PDEV_PID_AMLOGIC_A5;
  emmc_dev.did = PDEV_DID_AMLOGIC_SDMMC_C;
  emmc_dev.mmio_list = emmc_mmios;
  emmc_dev.mmio_count = std::size(emmc_mmios);
  emmc_dev.irq_list = emmc_irqs;
  emmc_dev.irq_count = std::size(emmc_irqs);
  emmc_dev.bti_list = emmc_btis;
  emmc_dev.bti_count = std::size(emmc_btis);
  emmc_dev.metadata_list = emmc_metadata;
  emmc_dev.metadata_count = std::size(emmc_metadata);
  emmc_dev.boot_metadata_list = emmc_boot_metadata;
  emmc_dev.boot_metadata_count = std::size(emmc_boot_metadata);

  // set alternate functions to enable EMMC
  gpio_impl_.SetAltFunction(A5_GPIOB(0), A5_GPIOB_0_EMMC_D0_FN);
  gpio_impl_.SetAltFunction(A5_GPIOB(1), A5_GPIOB_1_EMMC_D1_FN);
  gpio_impl_.SetAltFunction(A5_GPIOB(2), A5_GPIOB_2_EMMC_D2_FN);
  gpio_impl_.SetAltFunction(A5_GPIOB(3), A5_GPIOB_3_EMMC_D3_FN);
  gpio_impl_.SetAltFunction(A5_GPIOB(4), A5_GPIOB_4_EMMC_D4_FN);
  gpio_impl_.SetAltFunction(A5_GPIOB(5), A5_GPIOB_5_EMMC_D5_FN);
  gpio_impl_.SetAltFunction(A5_GPIOB(6), A5_GPIOB_6_EMMC_D6_FN);
  gpio_impl_.SetAltFunction(A5_GPIOB(7), A5_GPIOB_7_EMMC_D7_FN);
  gpio_impl_.SetAltFunction(A5_GPIOB(8), A5_GPIOB_8_EMMC_CLK_FN);
  gpio_impl_.SetAltFunction(A5_GPIOB(10), A5_GPIOB_10_EMMC_CMD_FN);
  gpio_impl_.SetAltFunction(A5_GPIOB(11), A5_GPIOB_11_EMMC_DS_FN);

  status = pbus_.AddComposite(&emmc_dev, reinterpret_cast<uint64_t>(av400_emmc_fragments),
                              std::size(av400_emmc_fragments), "pdev");
  if (status != ZX_OK) {
    zxlogf(ERROR, "SdEmmcInit could not add emmc_dev: %s\n", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}
}  // namespace av400
