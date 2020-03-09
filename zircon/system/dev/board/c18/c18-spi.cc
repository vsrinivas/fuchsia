// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/mmio/mmio.h>

#include <optional>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/metadata/spi.h>
#include <ddk/platform-defs.h>
#include <soc/mt8183/mt8183-hw.h>

#include "c18.h"

namespace board_c18 {

static const pbus_mmio_t spi_mmios[] = {
    /*     {
            .base = MT8183_SPI0_BASE,
            .length = MT8183_SPI_SIZE,
        },
        {
            .base = MT8183_SPI1_BASE,
            .length = MT8183_SPI_SIZE,
        }, */
    {
        .base = MT8183_SPI2_BASE,
        .length = MT8183_SPI_SIZE,
    },
    /*     {
            .base = MT8183_SPI3_BASE,
            .length = MT8183_SPI_SIZE,
        },
        {
            .base = MT8183_SPI4_BASE,
            .length = MT8183_SPI_SIZE,
        },
        {
            .base = MT8183_SPI5_BASE,
            .length = MT8183_SPI_SIZE,
        }, */
};

static const spi_channel_t spi_channels[] = {
    /*     {
            .bus_id = C18_SPI0,
            .cs = 0,  // index into matching chip-select map
        },
        {
            .bus_id = C18_SPI1,
            .cs = 0,  // index into matching chip-select map
        }, */
    {
        .bus_id = C18_SPI2,
        .cs = 0,  // index into matching chip-select map
    },
    /*     {
            .bus_id = C18_SPI3,
            .cs = 0,  // index into matching chip-select map
        },
        {
            .bus_id = C18_SPI4,
            .cs = 0,  // index into matching chip-select map
        },
        {
            .bus_id = C18_SPI5,
            .cs = 0,  // index into matching chip-select map
        }, */
};

static const pbus_metadata_t spi_metadata[] = {
    {
        .type = DEVICE_METADATA_SPI_CHANNELS,
        .data_buffer = spi_channels,
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
  zx_status_t status = pbus_.DeviceAdd(&spi_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d\n", __func__, status);
  }

  return status;
}

}  // namespace board_c18
