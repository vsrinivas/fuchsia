// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/sdmmc/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-a5/a5-gpio.h>
#include <soc/aml-a5/a5-hw.h>
#include <soc/aml-common/aml-sdmmc.h>

#include "src/devices/board/drivers/av400/av400-sdio-bind.h"
#include "src/devices/board/drivers/av400/av400.h"

namespace av400 {

static const pbus_mmio_t sdio_mmios[] = {
    {
        .base = A5_EMMC_A_BASE,
        .length = A5_EMMC_A_LENGTH,
    },
};

static const pbus_irq_t sdio_irqs[] = {
    {
        .irq = A5_SD_EMMC_A_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_bti_t sdio_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_SDIO,
    },
};

static aml_sdmmc_config_t config = {
    .supports_dma = true,
    .min_freq = 400'000,
    .max_freq = 200'000'000,
    .version_3 = true,
    .prefs = 0,
};

static const pbus_metadata_t sdio_metadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = reinterpret_cast<const uint8_t*>(&config),
        .data_size = sizeof(config),
    },
};

zx_status_t Av400::SdioInit() {
  zx_status_t status;

  pbus_dev_t sdio_dev = {};
  sdio_dev.name = "aml_sdio";
  sdio_dev.vid = PDEV_VID_AMLOGIC;
  sdio_dev.pid = PDEV_PID_GENERIC;
  sdio_dev.did = PDEV_DID_AMLOGIC_SDMMC_A;
  sdio_dev.mmio_list = sdio_mmios;
  sdio_dev.mmio_count = std::size(sdio_mmios);
  sdio_dev.irq_list = sdio_irqs;
  sdio_dev.irq_count = std::size(sdio_irqs);
  sdio_dev.bti_list = sdio_btis;
  sdio_dev.bti_count = std::size(sdio_btis);
  sdio_dev.metadata_list = sdio_metadata;
  sdio_dev.metadata_count = std::size(sdio_metadata);

  gpio_impl_.SetAltFunction(A5_GPIOX(0), A5_GPIOX_0_SDIO_D0_FN);
  gpio_impl_.SetAltFunction(A5_GPIOX(1), A5_GPIOX_1_SDIO_D1_FN);
  gpio_impl_.SetAltFunction(A5_GPIOX(2), A5_GPIOX_2_SDIO_D2_FN);
  gpio_impl_.SetAltFunction(A5_GPIOX(3), A5_GPIOX_3_SDIO_D3_FN);
  gpio_impl_.SetAltFunction(A5_GPIOX(4), A5_GPIOX_4_SDIO_CLK_FN);
  gpio_impl_.SetAltFunction(A5_GPIOX(5), A5_GPIOX_5_SDIO_CMD_FN);

  if ((status = pbus_.AddComposite(&sdio_dev, reinterpret_cast<uint64_t>(av400_sdio_fragments),
                                   std::size(av400_sdio_fragments), "pdev")) != ZX_OK) {
    zxlogf(ERROR, "SdInit could not add sdio_dev: %d", status);
    return status;
  }

  return ZX_OK;
}

}  // namespace av400
