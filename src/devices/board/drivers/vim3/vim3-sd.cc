// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/sdmmc/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/platform-defs.h>

#include <lib/ddk/metadata.h>
#include <soc/aml-a311d/a311d-gpio.h>
#include <soc/aml-a311d/a311d-hw.h>
#include <soc/aml-common/aml-sdmmc.h>

#include "vim3.h"

namespace vim3 {

static const pbus_mmio_t sd_mmios[] = {
    {
        .base = A311D_EMMC_B_BASE,
        .length = A311D_EMMC_B_LENGTH,
    },
};

static const pbus_irq_t sd_irqs[] = {
    {
        .irq = A311D_SD_EMMC_B_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_bti_t sd_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_SD,
    },
};

static aml_sdmmc_config_t config = {
    .supports_dma = true,
    .min_freq = 400'000,
    .max_freq = 50'000'000,
    .version_3 = true,
    .prefs = 0x1000'0000,  // Magic number to detect the SD slot.
};

static const pbus_metadata_t sd_metadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = reinterpret_cast<const uint8_t*>(&config),
        .data_size = sizeof(config),
    },
};

static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t i2c_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
      BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 0),
      BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x20),
};
static const device_fragment_part_t i2c_fragment[] = {
    {countof(root_match), root_match},
    {countof(i2c_match), i2c_match},
};
static const device_fragment_t fragments[] = {
    {"i2c", countof(i2c_fragment), i2c_fragment},
};

static const zx_bind_inst_t sdio_fn1_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_SDIO),
    BI_ABORT_IF(NE, BIND_SDIO_VID, 0x02d0),
    BI_ABORT_IF(NE, BIND_SDIO_FUNCTION, 1),
    BI_MATCH_IF(EQ, BIND_SDIO_PID, 0x4345),
};
static const zx_bind_inst_t sdio_fn2_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_SDIO),
    BI_ABORT_IF(NE, BIND_SDIO_VID, 0x02d0),
    BI_ABORT_IF(NE, BIND_SDIO_FUNCTION, 2),
    BI_MATCH_IF(EQ, BIND_SDIO_PID, 0x4345),
};
static const zx_bind_inst_t oob_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, A311D_GPIOC(6)),  // CD pin
};
static const device_fragment_part_t sdio_fn1_fragment[] = {
    {std::size(root_match), root_match},
    {std::size(sdio_fn1_match), sdio_fn1_match},
};
static const device_fragment_part_t sdio_fn2_fragment[] = {
    {std::size(root_match), root_match},
    {std::size(sdio_fn2_match), sdio_fn2_match},
};
static const device_fragment_part_t oob_gpio_fragment[] = {
    {std::size(root_match), root_match},
    {std::size(oob_gpio_match), oob_gpio_match},
};
static const device_fragment_t wifi_fragments[] = {
    {"sdio-function-1", std::size(sdio_fn1_fragment), sdio_fn1_fragment},
    {"sdio-function-2", std::size(sdio_fn2_fragment), sdio_fn2_fragment},
    {"gpio-oob", std::size(oob_gpio_fragment), oob_gpio_fragment},
};

zx_status_t Vim3::SdInit() {
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

  gpio_impl_.SetAltFunction(A311D_GPIOC(0), A311D_GPIOC_0_SDCARD_D0_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOC(1), A311D_GPIOC_1_SDCARD_D1_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOC(2), A311D_GPIOC_2_SDCARD_D2_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOC(3), A311D_GPIOC_3_SDCARD_D3_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOC(4), A311D_GPIOC_4_SDCARD_CLK_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOC(5), A311D_GPIOC_5_SDCARD_CMD_FN);

  if ((status = pbus_.CompositeDeviceAdd(&sd_dev, reinterpret_cast<uint64_t>(fragments),
                                         countof(fragments), UINT32_MAX)) != ZX_OK) {
    zxlogf(ERROR, "SdInit could not add sd_dev: %d", status);
    return status;
  }
    constexpr zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_BROADCOM},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_BCM4356},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_BCM_WIFI},
  };

  gpio_impl_.SetAltFunction(A311D_GPIOC(6), 0);

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = countof(props),
      .fragments = wifi_fragments,
      .fragments_count = countof(wifi_fragments),
      .coresident_device_index = 0,
      .metadata_list = nullptr,
      .metadata_count = 0,
  };

  if ((status = DdkAddComposite("wifi", &comp_desc)) != ZX_OK) {
   zxlogf(ERROR, "%s: device_add_composite failed: %d", __func__, status);
    return status;
  }


  return ZX_OK;
}

}  // namespace vim3
