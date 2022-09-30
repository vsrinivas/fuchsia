// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
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
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"
#include "src/devices/lib/fidl-metadata/spi.h"

#define HHI_SPICC_CLK_CNTL (0xf7 * 4)
#define spicc_0_clk_sel_fclk_div3 (3 << 7)
#define spicc_0_clk_en (1 << 6)
#define spicc_0_clk_div(x) ((x)-1)

namespace sherlock {
namespace fpbus = fuchsia_hardware_platform_bus;
using spi_channel_t = fidl_metadata::spi::Channel;

static const std::vector<fpbus::Mmio> spi_mmios{
    {{
        .base = T931_SPICC0_BASE,
        .length = 0x44,
    }},
};

static const std::vector<fpbus::Irq> spi_irqs{
    {{
        .irq = T931_SPICC0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
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

static const amlogic_spi::amlspi_config_t spi_config = {
    .capacity = 0,
    .period = 0,
    .bus_id = SHERLOCK_SPICC0,
    .cs_count = 1,
    .cs = {0},                                       // index into fragments list
    .clock_divider_register_value = (512 >> 1) - 1,  // SCLK = core clock / 512 = ~1.3 MHz
    .use_enhanced_clock_mode = true,
};

zx_status_t Sherlock::SpiInit() {
  // setup pinmux for the SPI bus
  // SPI_A
  gpio_impl_.SetAltFunction(T931_GPIOC(0), 5);         // MOSI
  gpio_impl_.SetAltFunction(T931_GPIOC(1), 5);         // MISO
  gpio_impl_.ConfigOut(GPIO_SPICC0_SS0, 1);            // SS0
  gpio_impl_.ConfigIn(T931_GPIOC(3), GPIO_PULL_DOWN);  // SCLK
  gpio_impl_.SetAltFunction(T931_GPIOC(3), 5);         // SCLK

  std::vector<fpbus::Metadata> spi_metadata;
  spi_metadata.emplace_back([&]() {
    fpbus::Metadata ret;
    ret.type() = DEVICE_METADATA_AMLSPI_CONFIG,
    ret.data() =
        std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&spi_config),
                             reinterpret_cast<const uint8_t*>(&spi_config) + sizeof(spi_config));
    return ret;
  }());

  auto spi_status = fidl_metadata::spi::SpiChannelsToFidl(spi_channels);
  if (spi_status.is_error()) {
    zxlogf(ERROR, "%s: failed to encode spi channels to fidl: %d", __func__,
           spi_status.error_value());
    return spi_status.error_value();
  }
  auto& data = spi_status.value();

  spi_metadata.emplace_back([&]() {
    fpbus::Metadata ret;
    ret.type() = DEVICE_METADATA_SPI_CHANNELS, ret.data() = std::move(data);
    return ret;
  }());

  fpbus::Node spi_dev;
  spi_dev.name() = "spi-0";
  spi_dev.vid() = PDEV_VID_AMLOGIC;
  spi_dev.pid() = PDEV_PID_GENERIC;
  spi_dev.did() = PDEV_DID_AMLOGIC_SPI;
  spi_dev.mmio() = spi_mmios;
  spi_dev.irq() = spi_irqs;

  spi_dev.metadata() = std::move(spi_metadata);

  // TODO(fxbug.dev/34010): fix this clock enable block when the clock driver can handle the
  // dividers
  {
    // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
    zx::unowned_resource resource(get_root_resource());
    std::optional<fdf::MmioBuffer> buf;
    zx_status_t status = fdf::MmioBuffer::Create(T931_HIU_BASE, T931_HIU_LENGTH, *resource,
                                                 ZX_CACHE_POLICY_UNCACHED_DEVICE, &buf);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: MmioBuffer::Create failed %d", __func__, status);
      return status;
    }

    // SPICC0 clock enable (666 MHz)
    buf->Write32(spicc_0_clk_sel_fclk_div3 | spicc_0_clk_en | spicc_0_clk_div(1),
                 HHI_SPICC_CLK_CNTL);
  }

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('SPI_');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, spi_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, spi_0_fragments,
                                               std::size(spi_0_fragments)),
      "pdev");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddComposite Spi(spi_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddComposite Spi(spi_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace sherlock
