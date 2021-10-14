// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <fbl/algorithm.h>
#include <soc/aml-a113/a113-hw.h>
#include <soc/aml-common/aml-sdmmc.h>
#include <soc/aml-s912/s912-gpio.h>
#include <soc/aml-s912/s912-hw.h>
#include <wifi/wifi-config.h>

#include "src/devices/board/drivers/vim2/aml_sdio_bind.h"
#include "src/devices/board/drivers/vim2/wifi_bind.h"
#include "vim-gpios.h"
#include "vim.h"

namespace vim {

static const pbus_mmio_t aml_sdmmc_mmios[] = {{
    .base = 0xD0070000,
    .length = 0x2000,
}};

static const pbus_irq_t aml_sdmmc_irqs[] = {
    {
        .irq = 248,
        // c++ initialization error
        .mode = 0,
        // c++ initialization error
    },
};

static const pbus_bti_t aml_sdmmc_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_SDIO,
    },
};

static aml_sdmmc_config_t config = {
    .supports_dma = true,
    .min_freq = 400000,
    .max_freq = 200000000,
    .version_3 = false,
    .prefs = 0,
};

static const wifi_config_t wifi_config = {
    .oob_irq_mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    .iovar_table = {},
    .cc_table = {},
};

static const pbus_metadata_t aml_sdmmc_metadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = reinterpret_cast<const uint8_t*>(&config),
        .data_size = sizeof(config),
    },
    {
        .type = DEVICE_METADATA_WIFI_CONFIG,
        .data_buffer = reinterpret_cast<const uint8_t*>(&wifi_config),
        .data_size = sizeof(wifi_config),
    },
};

static const pbus_dev_t aml_sdmmc_dev = []() {
  pbus_dev_t dev = {};

  dev.name = "aml-sdio";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_AMLOGIC_SDMMC_A;
  dev.mmio_list = aml_sdmmc_mmios;
  dev.mmio_count = countof(aml_sdmmc_mmios);
  dev.irq_list = aml_sdmmc_irqs;
  dev.irq_count = countof(aml_sdmmc_irqs);
  dev.bti_list = aml_sdmmc_btis;
  dev.bti_count = countof(aml_sdmmc_btis);
  dev.metadata_list = aml_sdmmc_metadata;
  dev.metadata_count = countof(aml_sdmmc_metadata);
  return dev;
}();

// Composite binding rules for wifi driver.

// Composite binding rules for SDIO.

zx_status_t Vim::SdioInit() {
  zx_status_t status;

  gpio_impl_.SetAltFunction(S912_WIFI_SDIO_D0, S912_WIFI_SDIO_D0_FN);
  gpio_impl_.SetAltFunction(S912_WIFI_SDIO_D1, S912_WIFI_SDIO_D1_FN);
  gpio_impl_.SetAltFunction(S912_WIFI_SDIO_D2, S912_WIFI_SDIO_D2_FN);
  gpio_impl_.SetAltFunction(S912_WIFI_SDIO_D3, S912_WIFI_SDIO_D3_FN);
  gpio_impl_.SetAltFunction(S912_WIFI_SDIO_CLK, S912_WIFI_SDIO_CLK_FN);
  gpio_impl_.SetAltFunction(S912_WIFI_SDIO_CMD, S912_WIFI_SDIO_CMD_FN);
  gpio_impl_.SetAltFunction(S912_WIFI_SDIO_WAKE_HOST, S912_WIFI_SDIO_WAKE_HOST_FN);

  status = pbus_.AddComposite(&aml_sdmmc_dev, reinterpret_cast<uint64_t>(aml_sdio_fragments),
                              std::size(aml_sdio_fragments), "pdev");
  if (status != ZX_OK) {
    zxlogf(ERROR, "SdioInit could not add aml_sdmmc_dev: %d", status);
    return status;
  }

  // Add a composite device for wifi driver.
  constexpr zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_BROADCOM},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_BCM4356},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_BCM_WIFI},
  };

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = countof(props),
      .fragments = wifi_fragments,
      .fragments_count = countof(wifi_fragments),
      .primary_fragment = "sdio-function-1",  // ???
      .spawn_colocated = true,
      .metadata_list = nullptr,
      .metadata_count = 0,
  };

  status = DdkAddComposite("wifi", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: device_add_composite failed: %d", __func__, status);
    return status;
  }

  return ZX_OK;
}
}  // namespace vim
