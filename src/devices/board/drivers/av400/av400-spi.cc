// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mmio/mmio.h>

#include <fbl/algorithm.h>
#include <soc/aml-a5/a5-gpio.h>
#include <soc/aml-common/aml-spi.h>

#include "av400.h"
#include "src/devices/board/drivers/av400/spi_1_bind.h"
#include "src/devices/lib/fidl-metadata/spi.h"

#define CLKCTRL_SPICC_CLK_CNTL (0x5d * 4)
#define spicc1_clk_sel_fclk_div2 (4 << 23)
#define spicc1_clk_en (1 << 22)
#define spicc1_clk_div(x) (((x)-1) << 16)

namespace av400 {
using spi_channel_t = fidl_metadata::spi::Channel;

static constexpr pbus_mmio_t spi_1_mmios[] = {
    {
        .base = A5_SPICC1_BASE,
        .length = A5_SPICC1_LENGTH,
    },
};

static constexpr pbus_irq_t spi_1_irqs[] = {
    {
        .irq = A5_SPICC1_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
};

static constexpr pbus_bti_t spi_1_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_SPI1,
    },
};

static constexpr spi_channel_t spi_1_channels[] = {
    {
        .bus_id = AV400_SPICC1,
        .cs = 0,  // index into matching chip-select map
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
};

static constexpr amlogic_spi::amlspi_config_t spi_1_config = {
    .capacity = 0,
    .period = 0,
    .bus_id = AV400_SPICC1,
    .cs_count = 1,
    .cs = {0},                                     // index into fragments list
    .clock_divider_register_value = (4 >> 1) - 1,  // SCLK = core clock / 4 = 10 MHz
    .use_enhanced_clock_mode = true,               // true  - div_reg = (div >> 1) - 1;
                                                   // false - div_reg = log2(div) - 2;
};

static pbus_dev_t spi_1_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "spi-1";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_AMLOGIC_SPI;
  dev.instance_id = 0;
  dev.mmio_list = spi_1_mmios;
  dev.mmio_count = std::size(spi_1_mmios);
  dev.irq_list = spi_1_irqs;
  dev.irq_count = std::size(spi_1_irqs);
  dev.bti_list = spi_1_btis;
  dev.bti_count = std::size(spi_1_btis);
  return dev;
}();

zx_status_t Av400::SpiInit() {
  zx_status_t status;
  constexpr uint32_t kSpiccClkValue =
      // src [25:23]:  4 - fclk_div2(1000M)-fixed
      // gate   [22]:  1 - enable clk
      // rate[21:16]: 24 - 1000M/(24+1) = 40M
      spicc1_clk_sel_fclk_div2 | spicc1_clk_en | spicc1_clk_div(25);

  {
    // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
    zx::unowned_resource resource(get_root_resource());
    std::optional<fdf::MmioBuffer> buf;
    status = fdf::MmioBuffer::Create(A5_CLK_BASE, A5_CLK_LENGTH, *resource,
                                     ZX_CACHE_POLICY_UNCACHED_DEVICE, &buf);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: MmioBuffer::Create failed %s", __func__, zx_status_get_string(status));
      return status;
    }

    buf->Write32(kSpiccClkValue, CLKCTRL_SPICC_CLK_CNTL);
  }

  // setup pinmux for SPICC1 bus arbiter.
  gpio_impl_.SetAltFunction(A5_GPIOT(10), A5_GPIOT_10_SPI_B_SS0_FN);  // SS0
  gpio_impl_.ConfigOut(A5_GPIOT(10), 1);

  gpio_impl_.SetAltFunction(A5_GPIOT(11), A5_GPIOT_11_SPI_B_SCLK_FN);  // SCLK
  gpio_impl_.SetDriveStrength(A5_GPIOT(11), 2500, nullptr);

  gpio_impl_.SetAltFunction(A5_GPIOT(12), A5_GPIOT_12_SPI_B_MOSI_FN);  // MOSI
  gpio_impl_.SetDriveStrength(A5_GPIOT(12), 2500, nullptr);

  gpio_impl_.SetAltFunction(A5_GPIOT(13), A5_GPIOT_13_SPI_B_MISO_FN);  // MISO
  gpio_impl_.SetDriveStrength(A5_GPIOT(13), 2500, nullptr);

  std::vector<pbus_metadata_t> spi_1_metadata;
  spi_1_metadata.emplace_back(pbus_metadata_t{
      .type = DEVICE_METADATA_AMLSPI_CONFIG,
      .data_buffer = reinterpret_cast<const uint8_t*>(&spi_1_config),
      .data_size = sizeof spi_1_config,
  });

  auto spi_status = fidl_metadata::spi::SpiChannelsToFidl(spi_1_channels);
  if (spi_status.is_error()) {
    zxlogf(ERROR, "%s: failed to encode spi channels to fidl: %d", __func__,
           spi_status.error_value());
    return spi_status.error_value();
  }
  auto& data = spi_status.value();

  spi_1_metadata.emplace_back(pbus_metadata_t{
      .type = DEVICE_METADATA_SPI_CHANNELS,
      .data_buffer = data.data(),
      .data_size = data.size(),
  });

  spi_1_dev.metadata_list = spi_1_metadata.data();
  spi_1_dev.metadata_count = spi_1_metadata.size();

  status = pbus_.AddComposite(&spi_1_dev, reinterpret_cast<uint64_t>(spi_1_fragments),
                              std::size(spi_1_fragments), "pdev");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %s", __func__, zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

}  // namespace av400
