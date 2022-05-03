// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/time.h>

#include <fbl/algorithm.h>
#include <soc/aml-common/aml-registers.h>
#include <soc/aml-common/aml-spi.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d3/s905d3-hw.h>

#include "nelson-gpios.h"
#include "nelson.h"
#include "src/devices/board/drivers/nelson/spi_0_bind.h"
#include "src/devices/board/drivers/nelson/spi_1_bind.h"
#include "src/devices/lib/fidl-metadata/spi.h"

#define HHI_SPICC_CLK_CNTL (0xf7 * 4)

#define spicc0_clk_sel_fclk_div3 (3 << 7)
#define spicc0_clk_en (1 << 6)
#define spicc0_clk_div(x) ((x)-1)

#define spicc1_clk_sel_fclk_div2 (4 << 23)
#define spicc1_clk_en (1 << 22)
#define spicc1_clk_div(x) (((x)-1) << 16)

namespace nelson {
using spi_channel_t = fidl_metadata::spi::Channel;

zx_status_t Nelson::SpiInit() {
  constexpr uint32_t kSpiccClkValue =
      // SPICC0 clock enable (666 MHz)
      spicc0_clk_sel_fclk_div3 | spicc0_clk_en | spicc0_clk_div(1) |

      // SPICC1 clock enable @200MHz (fclk_div2(1GHz) / N(5)).  For final SCLK frequency, see
      // CONREG[16:18] in the SPI controller.  This clock config produces a SCLK frequency of 50MHz
      // assuming a default value for CONREG[16:18].
      //
      // Some timing instability was observed which may have been an individual board artifact.  To
      // debug, consider configuring the SCLK=25MHz (i.e. set spicc1_cli_div(10)).
      spicc1_clk_sel_fclk_div2 | spicc1_clk_en | spicc1_clk_div(10);

  // TODO(fxbug.dev/34010): fix this clock enable block when the clock driver can handle the
  // dividers
  {
    // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
    zx::unowned_resource resource(get_root_resource());
    std::optional<fdf::MmioBuffer> buf;
    zx_status_t status = fdf::MmioBuffer::Create(S905D3_HIU_BASE, S905D3_HIU_LENGTH, *resource,
                                                 ZX_CACHE_POLICY_UNCACHED_DEVICE, &buf);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: MmioBuffer::Create failed %d", __func__, status);
      return status;
    }

    buf->Write32(kSpiccClkValue, HHI_SPICC_CLK_CNTL);
  }

  zx_status_t status0 = Spi0Init();
  zx_status_t status1 = Spi1Init();
  return status0 == ZX_OK ? status1 : status0;
}

zx_status_t Nelson::Spi0Init() {
  static const pbus_mmio_t spi_0_mmios[] = {
      {
          .base = S905D3_SPICC0_BASE,
          .length = S905D3_SPICC0_LENGTH,
      },
  };

  static const pbus_irq_t spi_0_irqs[] = {
      {
          .irq = S905D3_SPICC0_IRQ,
          .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
      },
  };

  static const spi_channel_t spi_0_channels[] = {
      {
          .bus_id = NELSON_SPICC0,
          .cs = 0,  // index into matching chip-select map
          .vid = PDEV_VID_NORDIC,
          .pid = PDEV_PID_NORDIC_NRF52811,
          .did = PDEV_DID_NORDIC_THREAD,
      },
  };

  static const amlogic_spi::amlspi_config_t spi_0_config = {
      .capacity = 0,
      .period = 0,
      .bus_id = NELSON_SPICC0,
      .cs_count = 1,
      .cs = {0},                                       // index into fragments list
      .clock_divider_register_value = (512 >> 1) - 1,  // SCLK = core clock / 512 = ~1.3 MHz
      .use_enhanced_clock_mode = true,
  };

  static pbus_dev_t spi_0_dev = []() {
    pbus_dev_t dev = {};
    dev.name = "spi-0";
    dev.vid = PDEV_VID_AMLOGIC;
    dev.pid = PDEV_PID_GENERIC;
    dev.did = PDEV_DID_AMLOGIC_SPI;
    dev.instance_id = 0;
    dev.mmio_list = spi_0_mmios;
    dev.mmio_count = std::size(spi_0_mmios);
    dev.irq_list = spi_0_irqs;
    dev.irq_count = std::size(spi_0_irqs);
    return dev;
  }();

  gpio_impl_.SetAltFunction(GPIO_SOC_SPI_A_MOSI, 5);  // MOSI
  gpio_impl_.SetDriveStrength(GPIO_SOC_SPI_A_MOSI, 2500, nullptr);

  gpio_impl_.SetAltFunction(GPIO_SOC_SPI_A_MISO, 5);  // MISO
  gpio_impl_.SetDriveStrength(GPIO_SOC_SPI_A_MISO, 2500, nullptr);

  gpio_impl_.SetAltFunction(GPIO_SOC_SPI_A_SS0, 0);
  gpio_impl_.ConfigOut(GPIO_SOC_SPI_A_SS0, 1);  // SS0

  // SCLK must be pulled down to prevent SPI bit errors.
  gpio_impl_.ConfigIn(GPIO_SOC_SPI_A_SCLK, GPIO_PULL_DOWN);
  gpio_impl_.SetAltFunction(GPIO_SOC_SPI_A_SCLK, 5);  // SCLK
  gpio_impl_.SetDriveStrength(GPIO_SOC_SPI_A_SCLK, 2500, nullptr);

  std::vector<pbus_metadata_t> spi_0_metadata;
  spi_0_metadata.emplace_back(pbus_metadata_t{
      .type = DEVICE_METADATA_AMLSPI_CONFIG,
      .data_buffer = reinterpret_cast<const uint8_t*>(&spi_0_config),
      .data_size = sizeof spi_0_config,
  });

  auto spi_status = fidl_metadata::spi::SpiChannelsToFidl(spi_0_channels);
  if (spi_status.is_error()) {
    zxlogf(ERROR, "%s: failed to encode spi channels to fidl: %d", __func__,
           spi_status.error_value());
    return spi_status.error_value();
  }
  auto& data = spi_status.value();

  spi_0_metadata.emplace_back(pbus_metadata_t{
      .type = DEVICE_METADATA_SPI_CHANNELS,
      .data_buffer = data.data(),
      .data_size = data.size(),
  });

  spi_0_dev.metadata_list = spi_0_metadata.data();
  spi_0_dev.metadata_count = spi_0_metadata.size();

  zx_status_t status = pbus_.AddComposite(&spi_0_dev, reinterpret_cast<uint64_t>(spi_0_fragments),
                                          std::size(spi_0_fragments), "pdev");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

zx_status_t Nelson::Spi1Init() {
  static const pbus_mmio_t spi_1_mmios[] = {
      {
          .base = S905D3_SPICC1_BASE,
          .length = S905D3_SPICC1_LENGTH,
      },
  };

  static const pbus_irq_t spi_1_irqs[] = {
      {
          .irq = S905D3_SPICC1_IRQ,
          .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
      },
  };

  static const spi_channel_t spi_1_channels[] = {
      // Radar sensor head.
      {
          .bus_id = NELSON_SPICC1,
          .cs = 0,  // index into matching chip-select map
          .vid = PDEV_VID_INFINEON,
          .pid = PDEV_PID_INFINEON_BGT60TR13C,
          .did = PDEV_DID_RADAR_SENSOR,
      },
  };

  static const amlogic_spi::amlspi_config_t spi_1_config = {
      .capacity = 0,
      .period = 0,
      .bus_id = NELSON_SPICC1,
      .cs_count = 1,
      .cs = {0},                          // index into fragments list
      .clock_divider_register_value = 0,  // SCLK = core clock / 4 = 25 MHz
      .use_enhanced_clock_mode = false,
  };

  static pbus_dev_t spi_1_dev = []() {
    pbus_dev_t dev = {};
    dev.name = "spi-1";
    dev.vid = PDEV_VID_AMLOGIC;
    dev.pid = PDEV_PID_GENERIC;
    dev.did = PDEV_DID_AMLOGIC_SPI;
    dev.instance_id = 1;
    dev.mmio_list = spi_1_mmios;
    dev.mmio_count = std::size(spi_1_mmios);
    dev.irq_list = spi_1_irqs;
    dev.irq_count = std::size(spi_1_irqs);
    return dev;
  }();

  // setup pinmux for SPICC1 bus arbiter.
  gpio_impl_.SetAltFunction(GPIO_SOC_SPI_B_MOSI, 3);  // MOSI
  gpio_impl_.SetDriveStrength(GPIO_SOC_SPI_B_MOSI, 2500, nullptr);

  gpio_impl_.SetAltFunction(GPIO_SOC_SPI_B_MISO, 3);  // MISO
  gpio_impl_.SetDriveStrength(GPIO_SOC_SPI_B_MISO, 2500, nullptr);

  gpio_impl_.ConfigOut(GPIO_SOC_SPI_B_SS0, 1);  // SS0

  gpio_impl_.SetAltFunction(GPIO_SOC_SPI_B_SCLK, 3);  // SCLK
  gpio_impl_.SetDriveStrength(GPIO_SOC_SPI_B_SCLK, 2500, nullptr);

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

  zx_status_t status = pbus_.AddComposite(&spi_1_dev, reinterpret_cast<uint64_t>(spi_1_fragments),
                                          std::size(spi_1_fragments), "pdev");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace nelson
