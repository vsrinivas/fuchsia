// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/mmio/mmio.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/metadata/spi.h>
#include <ddk/platform-defs.h>
#include <fbl/algorithm.h>
#include <soc/vs680/vs680-spi.h>

#include "vs680-evk.h"

namespace board_vs680_evk {

static const pbus_mmio_t spi_mmios[] = {
    {
          .base = vs680::kSpi1Base,
          .length = vs680::kSpiSize,
    },
};

static const pbus_irq_t spi_irqs[] = {
    {
        .irq = vs680::kSpi1Irq,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
};

static const spi_channel_t spi_channels[] = {
    {
        .bus_id = 0,
        .cs = 0,
        .vid = PDEV_VID_GENERIC,
        .pid = PDEV_PID_GENERIC,
        .did = PDEV_DID_TEST_SPI,
    },
    {
        .bus_id = 0,
        .cs = 1,
        .vid = PDEV_VID_GENERIC,
        .pid = PDEV_PID_GENERIC,
        .did = PDEV_DID_TEST_SPI,
    },
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
  dev.name = "spi";
  dev.vid = PDEV_VID_GENERIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_DW_SPI;
  dev.mmio_list = spi_mmios;
  dev.mmio_count = countof(spi_mmios);
  dev.irq_list = spi_irqs;
  dev.irq_count = countof(spi_irqs);
  dev.metadata_list = spi_metadata;
  dev.metadata_count = countof(spi_metadata);
  return dev;
}();

zx_status_t Vs680Evk::SpiInit() {
  ddk::GpioImplProtocolClient gpio(parent());
  if (!gpio.is_valid()) {
    zxlogf(ERROR, "%s: Failed to create GPIO protocol client", __PRETTY_FUNCTION__);
    return ZX_ERR_INTERNAL;
  }

  zx_status_t status;
  if ((status = gpio.SetAltFunction(vs680::kSpi1Cs0, vs680::kSpi1Cs0AltFunction)) != ZX_OK ||
      (status = gpio.SetAltFunction(vs680::kSpi1Cs1, vs680::kSpi1Cs1AltFunction)) != ZX_OK ||
      (status = gpio.SetAltFunction(vs680::kSpi1Clk, vs680::kSpi1ClkAltFunction)) != ZX_OK ||
      (status = gpio.SetAltFunction(vs680::kSpi1Mosi, vs680::kSpi1MosiAltFunction)) != ZX_OK ||
      (status = gpio.SetAltFunction(vs680::kSpi1Miso, vs680::kSpi1MisoAltFunction)) != ZX_OK) {
    zxlogf(ERROR, "%s: GPIO SetAltFunction failed %d", __PRETTY_FUNCTION__, status);
    return status;
  }

  status = pbus_.DeviceAdd(&spi_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __PRETTY_FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_vs680_evk
