// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/sdmmc/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-a311d/a311d-gpio.h>
#include <soc/aml-a311d/a311d-hw.h>
#include <soc/aml-common/aml-sdmmc.h>

#include "vim3-gpios.h"
#include "vim3.h"

namespace vim3 {

static const pbus_mmio_t sdio_mmios[] = {
    {
        .base = A311D_EMMC_A_BASE,
        .length = A311D_EMMC_A_LENGTH,
    },
};

static const pbus_irq_t sdio_irqs[] = {
    {
        .irq = A311D_SD_EMMC_A_IRQ,
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

static const zx_bind_inst_t wifi_pwren_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, A311D_GPIOX(6)),
};
static const device_fragment_part_t wifi_pwren_gpio_fragment[] = {
    {std::size(wifi_pwren_gpio_match), wifi_pwren_gpio_match},
};
static const device_fragment_t sdio_fragments[] = {
    {"gpio-wifi-power-on", std::size(wifi_pwren_gpio_fragment), wifi_pwren_gpio_fragment},
};

zx_status_t Vim3::SdioInit() {
  zx_status_t status;

  pbus_dev_t sdio_dev = {};
  sdio_dev.name = "aml_sdio";
  sdio_dev.vid = PDEV_VID_AMLOGIC;
  sdio_dev.pid = PDEV_PID_GENERIC;
  sdio_dev.did = PDEV_DID_AMLOGIC_SDMMC_A;
  sdio_dev.mmio_list = sdio_mmios;
  sdio_dev.mmio_count = countof(sdio_mmios);
  sdio_dev.irq_list = sdio_irqs;
  sdio_dev.irq_count = countof(sdio_irqs);
  sdio_dev.bti_list = sdio_btis;
  sdio_dev.bti_count = countof(sdio_btis);
  sdio_dev.metadata_list = sdio_metadata;
  sdio_dev.metadata_count = countof(sdio_metadata);

  gpio_impl_.SetAltFunction(A311D_SDIO_D0, A311D_GPIOX_0_SDIO_D0_FN);
  gpio_impl_.SetAltFunction(A311D_SDIO_D1, A311D_GPIOX_1_SDIO_D1_FN);
  gpio_impl_.SetAltFunction(A311D_SDIO_D2, A311D_GPIOX_2_SDIO_D2_FN);
  gpio_impl_.SetAltFunction(A311D_SDIO_D3, A311D_GPIOX_3_SDIO_D3_FN);
  gpio_impl_.SetAltFunction(A311D_SDIO_CLK, A311D_GPIOX_4_SDIO_CLK_FN);
  gpio_impl_.SetAltFunction(A311D_SDIO_CMD, A311D_GPIOX_5_SDIO_CMD_FN);

  if ((status = pbus_.CompositeDeviceAdd(&sdio_dev, reinterpret_cast<uint64_t>(sdio_fragments),
                                         countof(sdio_fragments), nullptr)) != ZX_OK) {
    zxlogf(ERROR, "SdInit could not add sdio_dev: %d", status);
    return status;
  }

  return ZX_OK;
}

}  // namespace vim3
