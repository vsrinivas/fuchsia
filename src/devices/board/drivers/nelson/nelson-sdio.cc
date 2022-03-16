// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/align.h>

#include <ddk/metadata/init-step.h>
#include <fbl/algorithm.h>
#include <hwreg/bitfields.h>
#include <soc/aml-common/aml-sdmmc.h>
#include <soc/aml-s905d3/s905d3-gpio.h>
#include <soc/aml-s905d3/s905d3-hw.h>
#include <wifi/wifi-config.h>

#include "nelson-gpios.h"
#include "nelson.h"
#include "src/devices/board/drivers/nelson/nelson_aml_sdio_bind.h"
#include "src/devices/board/drivers/nelson/nelson_wifi_bind.h"

namespace nelson {

static const pbus_boot_metadata_t wifi_boot_metadata[] = {
    {
        .zbi_type = DEVICE_METADATA_MAC_ADDRESS,
        .zbi_extra = MACADDR_WIFI,
    },
};

static const pbus_mmio_t sd_emmc_mmios[] = {
    {
        .base = S905D3_EMMC_A_SDIO_BASE,
        .length = S905D3_EMMC_A_SDIO_LENGTH,
    },
    {
        .base = S905D3_GPIO_BASE,
        .length = S905D3_GPIO_LENGTH,
    },
    {
        .base = S905D3_HIU_BASE,
        .length = S905D3_HIU_LENGTH,
    },
};

static const pbus_irq_t sd_emmc_irqs[] = {
    {
        .irq = S905D3_EMMC_A_SDIO_IRQ,
        .mode = 0,
    },
};

static const pbus_bti_t sd_emmc_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_SDIO,
    },
};

static aml_sdmmc_config_t config = {
    .supports_dma = true,
    .min_freq = 400'000,
    .max_freq = 208'000'000,
    .version_3 = true,
    .prefs = 0,
    .use_new_tuning = true,
};

constexpr wifi_config_t wifi_config = {
    .oob_irq_mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    .iovar_table =
        {
            {IOVAR_STR_TYPE, {"ampdu_ba_wsize"}, 32},
            {IOVAR_STR_TYPE, {"stbc_tx"}, 0},  // since tx_streams is 1
            {IOVAR_STR_TYPE, {"stbc_rx"}, 1},
            {IOVAR_CMD_TYPE, {.iovar_cmd = BRCMF_C_SET_PM}, 0},
            {IOVAR_CMD_TYPE, {.iovar_cmd = BRCMF_C_SET_FAKEFRAG}, 1},
            {IOVAR_LIST_END_TYPE, {{0}}, 0},
        },
    .cc_table =
        {
            {"WW", 2},   {"AU", 924}, {"CA", 902}, {"US", 844}, {"GB", 890}, {"BE", 890},
            {"BG", 890}, {"CZ", 890}, {"DK", 890}, {"DE", 890}, {"EE", 890}, {"IE", 890},
            {"GR", 890}, {"ES", 890}, {"FR", 890}, {"HR", 890}, {"IT", 890}, {"CY", 890},
            {"LV", 890}, {"LT", 890}, {"LU", 890}, {"HU", 890}, {"MT", 890}, {"NL", 890},
            {"AT", 890}, {"PL", 890}, {"PT", 890}, {"RO", 890}, {"SI", 890}, {"SK", 890},
            {"FI", 890}, {"SE", 890}, {"EL", 890}, {"IS", 890}, {"LI", 890}, {"TR", 890},
            {"CH", 890}, {"NO", 890}, {"JP", 3},   {"KR", 3},   {"TW", 3},   {"IN", 3},
            {"SG", 3},   {"MX", 3},   {"NZ", 3},   {"", 0},
        },
};

static const pbus_metadata_t sd_emmc_metadata[] = {
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

static const pbus_dev_t sd_emmc_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-sdio";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_AMLOGIC_SDMMC_A;
  dev.mmio_list = sd_emmc_mmios;
  dev.mmio_count = std::size(sd_emmc_mmios);
  dev.irq_list = sd_emmc_irqs;
  dev.irq_count = std::size(sd_emmc_irqs);
  dev.bti_list = sd_emmc_btis;
  dev.bti_count = std::size(sd_emmc_btis);
  dev.metadata_list = sd_emmc_metadata;
  dev.metadata_count = std::size(sd_emmc_metadata);
  dev.boot_metadata_list = wifi_boot_metadata;
  dev.boot_metadata_count = std::size(wifi_boot_metadata);
  return dev;
}();

// Composite binding rules for wifi driver.

// Composite binding rules for SDIO.

zx_status_t Nelson::SdioInit() {
  zx_status_t status;

  gpio_impl_.SetAltFunction(S905D3_WIFI_SDIO_D0, S905D3_WIFI_SDIO_D0_FN);
  gpio_impl_.SetAltFunction(S905D3_WIFI_SDIO_D1, S905D3_WIFI_SDIO_D1_FN);
  gpio_impl_.SetAltFunction(S905D3_WIFI_SDIO_D2, S905D3_WIFI_SDIO_D2_FN);
  gpio_impl_.SetAltFunction(S905D3_WIFI_SDIO_D3, S905D3_WIFI_SDIO_D3_FN);
  gpio_impl_.SetAltFunction(S905D3_WIFI_SDIO_CLK, S905D3_WIFI_SDIO_CLK_FN);
  gpio_impl_.SetAltFunction(S905D3_WIFI_SDIO_CMD, S905D3_WIFI_SDIO_CMD_FN);
  gpio_impl_.SetAltFunction(S905D3_WIFI_SDIO_WAKE_HOST, 0);

  gpio_impl_.SetDriveStrength(GPIO_SOC_WIFI_SDIO_D0, 4000, nullptr);
  gpio_impl_.SetDriveStrength(GPIO_SOC_WIFI_SDIO_D1, 4000, nullptr);
  gpio_impl_.SetDriveStrength(GPIO_SOC_WIFI_SDIO_D2, 4000, nullptr);
  gpio_impl_.SetDriveStrength(GPIO_SOC_WIFI_SDIO_D3, 4000, nullptr);
  gpio_impl_.SetDriveStrength(GPIO_SOC_WIFI_SDIO_CLK, 4000, nullptr);
  gpio_impl_.SetDriveStrength(GPIO_SOC_WIFI_SDIO_CMD, 4000, nullptr);

  status = pbus_.AddComposite(&sd_emmc_dev, reinterpret_cast<uint64_t>(aml_sdio_fragments),
                              std::size(aml_sdio_fragments), "pdev");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd sd_emmc failed: %d", __func__, status);
    return status;
  }

  // Add a composite device for wifi driver.
  const zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_BROADCOM},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_BCM43458},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_BCM_WIFI},
  };

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = std::size(props),
      .fragments = wifi_fragments,
      .fragments_count = std::size(wifi_fragments),
      .primary_fragment = "sdio-function-1",  // ???
      .spawn_colocated = true,
      .metadata_list = nullptr,
      .metadata_count = 0,
  };

  status = DdkAddComposite("wifi", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAddComposite failed: %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace nelson
