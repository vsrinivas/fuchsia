// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/mmio/mmio.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/metadata/spi.h>
#include <ddk/platform-defs.h>
#include <fbl/algorithm.h>
#include <soc/aml-common/aml-spi.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d3/s905d3-hw.h>

#include "nelson-gpios.h"
#include "nelson.h"

#define HHI_SPICC_CLK_CNTL (0xf7 * 4)
#define spicc1_clk_sel_fclk_div2 (4 << 23)
#define spicc1_clk_en (1 << 22)
#define spicc1_clk_div(x) (((x)-1) << 16)

namespace nelson {

static const pbus_mmio_t spi_mmios[] = {
    {
        .base = S905D3_SPICC0_BASE,
        .length = S905D3_SPICC0_LENGTH,
    },
    {
        .base = S905D3_SPICC1_BASE,
        .length = S905D3_SPICC1_LENGTH,
    },
};

static const pbus_irq_t spi_irqs[] = {
    {
        .irq = S905D3_SPICC0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D3_SPICC1_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const spi_channel_t spi_channels[] = {
    // Radar sensor head.
    {
        .bus_id = NELSON_SPICC1,
        .cs = 0,  // index into matching chip-select map
        .vid = PDEV_VID_INFINEON,
        .pid = PDEV_PID_INFINEON_BGT60TR13C,
        .did = PDEV_DID_RADAR_SENSOR,
    }};

static const amlspi_cs_map_t spi_cs_map[] = {
    {.bus_id = NELSON_SPICC0, .cs_count = 0, .cs = {}},  // Unused.
    {
        .bus_id = NELSON_SPICC1, .cs_count = 1, .cs = {0}  // index into fragments list
    },
};

static const pbus_metadata_t spi_metadata[] = {
    {
        .type = DEVICE_METADATA_SPI_CHANNELS,
        .data_buffer = spi_channels,
        .data_size = sizeof spi_channels,
    },
    {
        .type = DEVICE_METADATA_AMLSPI_CS_MAPPING,
        .data_buffer = &spi_cs_map,
        .data_size = sizeof spi_cs_map,
    },
};

static pbus_dev_t spi_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "spi";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_AMLOGIC_SPI;
  dev.mmio_list = spi_mmios;
  dev.mmio_count = countof(spi_mmios);
  dev.irq_list = spi_irqs;
  dev.irq_count = countof(spi_irqs);
  dev.metadata_list = spi_metadata;
  dev.metadata_count = countof(spi_metadata);
  return dev;
}();

// composite binding rules
static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static constexpr zx_bind_inst_t gpio_spicc1_ss0_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_SPICC1_SS0),
};
static constexpr device_fragment_part_t gpio_spicc1_ss0_fragment[] = {
    {std::size(root_match), root_match},
    {std::size(gpio_spicc1_ss0_match), gpio_spicc1_ss0_match},
};
static constexpr device_fragment_t fragments[] = {
    {"gpio", std::size(gpio_spicc1_ss0_fragment), gpio_spicc1_ss0_fragment},
};

zx_status_t Nelson::SpiInit() {
  // setup pinmux for SPICC1 bus arbiter.
  gpio_impl_.SetAltFunction(S905D2_GPIOH(4), 3);         // MOSI
  gpio_impl_.SetAltFunction(S905D2_GPIOH(5), 3);         // MISO
  gpio_impl_.ConfigOut(GPIO_SPICC1_SS0, 1);              // SS0
  gpio_impl_.ConfigIn(S905D2_GPIOH(7), GPIO_PULL_DOWN);  // SCLK
  gpio_impl_.SetAltFunction(S905D2_GPIOH(7), 3);         // SCLK

  // TODO(fxbug.dev/34010): fix this clock enable block when the clock driver can handle the
  // dividers
  {
    // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
    zx::unowned_resource resource(get_root_resource());
    std::optional<ddk::MmioBuffer> buf;
    zx_status_t status = ddk::MmioBuffer::Create(S905D3_HIU_BASE, S905D3_HIU_LENGTH, *resource,
                                                 ZX_CACHE_POLICY_UNCACHED_DEVICE, &buf);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: MmioBuffer::Create failed %d", __func__, status);
      return status;
    }

    // SPICC1 clock enable @200MHz (fclk_div2(1GHz) / N(5)).  For final SCLK frequency, see
    // CONREG[16:18] in the SPI controller.  This clock config produces a SCLK frequency of 50MHz
    // assuming a default value for CONREG[16:18].
    //
    // Some timing instability was observed which may have been an individual board artifact.  To
    // debug, consider configuring the SCLK=25MHz (i.e. set spicc1_cli_div(10)).
    buf->Write32(spicc1_clk_sel_fclk_div2 | spicc1_clk_en | spicc1_clk_div(5), HHI_SPICC_CLK_CNTL);
  }

  zx_status_t status =
      pbus_.CompositeDeviceAdd(&spi_dev, fragments, std::size(fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace nelson
