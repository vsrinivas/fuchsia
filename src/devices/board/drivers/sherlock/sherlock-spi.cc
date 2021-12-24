// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mmio/mmio.h>

#include <fbl/algorithm.h>
#include <soc/aml-common/aml-registers.h>
#include <soc/aml-common/aml-spi.h>
#include <soc/aml-t931/t931-gpio.h>

#include "sherlock-gpios.h"
#include "sherlock.h"
#include "src/devices/board/drivers/sherlock/sherlock-spi-bind.h"
#include "src/devices/lib/fidl-metadata/spi.h"

#define HHI_SPICC_CLK_CNTL (0xf7 * 4)
#define spicc_0_clk_sel_fclk_div5 (5 << 7)
#define spicc_0_clk_en (1 << 6)
#define spicc_0_clk_div(x) ((x)-1)

namespace sherlock {
using spi_channel_t = fidl_metadata::spi::Channel;

static const pbus_mmio_t spi_mmios[] = {
    {
        .base = T931_SPICC0_BASE,
        .length = 0x44,
    },
};

static const spi_channel_t spi_channels[] = {
    // Thread SPI
    {
        .bus_id = SHERLOCK_SPICC0,
        .cs = 0,  // index into matching chip-select map
        .vid = PDEV_VID_NORDIC,
        .pid = PDEV_PID_NORDIC_NRF52840,
        .did = PDEV_DID_NORDIC_THREAD,
    },
};

static const amlspi_config_t spi_config = {
    .capacity = 0,
    .period = 0,
    .bus_id = SHERLOCK_SPICC0,
    .cs_count = 1,
    .cs = {0},  // index into fragments list
};

static pbus_dev_t spi_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "spi-0";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_AMLOGIC_SPI;
  dev.mmio_list = spi_mmios;
  dev.mmio_count = std::size(spi_mmios);
  return dev;
}();

zx_status_t Sherlock::SpiInit() {
  // setup pinmux for the SPI bus
  // SPI_A
  gpio_impl_.SetAltFunction(T931_GPIOC(0), 5);         // MOSI
  gpio_impl_.SetAltFunction(T931_GPIOC(1), 5);         // MISO
  gpio_impl_.ConfigOut(GPIO_SPICC0_SS0, 1);            // SS0
  gpio_impl_.ConfigIn(T931_GPIOC(3), GPIO_PULL_DOWN);  // SCLK
  gpio_impl_.SetAltFunction(T931_GPIOC(3), 5);         // SCLK

  std::vector<pbus_metadata_t> spi_metadata;
  spi_metadata.emplace_back(pbus_metadata_t{
      .type = DEVICE_METADATA_AMLSPI_CONFIG,
      .data_buffer = reinterpret_cast<const uint8_t*>(&spi_config),
      .data_size = sizeof spi_config,
  });

  auto spi_status = fidl_metadata::spi::SpiChannelsToFidl(spi_channels);
  if (spi_status.is_error()) {
    zxlogf(ERROR, "%s: failed to encode spi channels to fidl: %d", __func__,
           spi_status.error_value());
    return spi_status.error_value();
  }
  auto& data = spi_status.value();

  spi_metadata.emplace_back(pbus_metadata_t{
      .type = DEVICE_METADATA_SPI_CHANNELS,
      .data_buffer = data.data(),
      .data_size = data.size(),
  });

  spi_dev.metadata_list = spi_metadata.data();
  spi_dev.metadata_count = spi_metadata.size();

  // TODO(fxbug.dev/34010): fix this clock enable block when the clock driver can handle the
  // dividers
  {
    // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
    zx::unowned_resource resource(get_root_resource());
    std::optional<ddk::MmioBuffer> buf;
    zx_status_t status = ddk::MmioBuffer::Create(T931_HIU_BASE, T931_HIU_LENGTH, *resource,
                                                 ZX_CACHE_POLICY_UNCACHED_DEVICE, &buf);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: MmioBuffer::Create failed %d", __func__, status);
      return status;
    }

    // SPICC0 clock enable
    buf->Write32(spicc_0_clk_sel_fclk_div5 | spicc_0_clk_en | spicc_0_clk_div(10),
                 HHI_SPICC_CLK_CNTL);
  }

  zx_status_t status = pbus_.AddComposite(&spi_dev, reinterpret_cast<uint64_t>(spi_0_fragments),
                                          std::size(spi_0_fragments), "pdev");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace sherlock
