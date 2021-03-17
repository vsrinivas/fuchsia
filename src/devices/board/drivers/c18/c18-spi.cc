// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mmio/mmio.h>

#include <optional>

#include <lib/ddk/metadata.h>
#include <ddk/metadata/spi.h>
#include <soc/mt8183/mt8183-hw.h>

#include "c18.h"

namespace board_c18 {

constexpr uint32_t kTopCkGenRegBase = 0x10000000;
constexpr uint32_t kTopCkGenRegSize = 0x1000;
constexpr uint32_t kClkCfg3SetOffset = 0x74;
constexpr uint32_t kClkCfg3ClrOffset = 0x78;
constexpr uint32_t kClkCfgUpdateOffset = 0x04;
constexpr uint32_t kSpiCkUpdateShift = 15;
constexpr uint32_t kSpiClockOffShift = 31;
constexpr uint32_t kClkSpiSelShift = 24;
constexpr uint32_t kMainPllD5D2 = 1;

static const pbus_mmio_t spi_mmios[] = {
    {
        .base = MT8183_SPI2_BASE,
        .length = MT8183_SPI_SIZE,
    },
};

static const spi_channel_t spi_channels[] = {
    {
        .bus_id = C18_SPI2,
        .cs = 0,  // index into matching chip-select map
    },
};

static const pbus_metadata_t spi_metadata[] = {
    {
        .type = DEVICE_METADATA_SPI_CHANNELS,
        .data_buffer = reinterpret_cast<const uint8_t*>(spi_channels),
        .data_size = sizeof spi_channels,
    },
};

static pbus_dev_t spi_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "c18-spi";
  dev.vid = PDEV_VID_MEDIATEK;
  dev.did = PDEV_DID_MEDIATEK_SPI;
  dev.mmio_list = spi_mmios;
  dev.mmio_count = countof(spi_mmios);
  dev.metadata_list = spi_metadata;
  dev.metadata_count = countof(spi_metadata);
  return dev;
}();

zx_status_t C18::SpiInit() {
  // 1. Configure GPIOs
  gpio_impl_.SetAltFunction(MT8183_GPIO_SPI2_MI, 7);
  gpio_impl_.ConfigOut(MT8183_GPIO_SPI2_CSB, 1);
  gpio_impl_.SetAltFunction(MT8183_GPIO_SPI2_MO, 7);
  gpio_impl_.SetAltFunction(MT8183_GPIO_SPI2_CLK, 7);

  {
    // Enable Clock
    std::optional<ddk::MmioBuffer> mmio2;
    auto status = ddk::MmioBuffer::Create(
        kTopCkGenRegBase, kTopCkGenRegSize,
        // Please do not use get_root_resource() in new code (fxbug.dev/31358).
        zx::resource(get_root_resource()), ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio2);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: MmioBuffer Create failed %d ", __PRETTY_FUNCTION__, status);
      return status;
    }

    mmio2->SetBits32(1 << kSpiClockOffShift, kClkCfg3ClrOffset);
    mmio2->SetBits32(1 << kSpiCkUpdateShift, kClkCfgUpdateOffset);
    mmio2->SetBits32(kMainPllD5D2 << kClkSpiSelShift, kClkCfg3SetOffset);
    mmio2->SetBits32(1 << kSpiCkUpdateShift, kClkCfgUpdateOffset);
  }

  zx_status_t status = pbus_.DeviceAdd(&spi_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
  }

  return status;
}

}  // namespace board_c18
