// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mmio/mmio.h>

#include <fbl/algorithm.h>
#include <soc/aml-a1/a1-gpio.h>
#include <soc/aml-a1/a1-hw.h>
#include <soc/aml-common/aml-spi.h>

#include "clover.h"
#include "src/devices/board/drivers/clover/clover-spi-0-bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"
#include "src/devices/lib/fidl-metadata/spi.h"

// For 'fdf::MmioBuffer::Create'
// |base| is guaranteed to be page aligned.
#define A1_CLK_BASE_ALIGN 0xfe000000
#define A1_CLK_LENGTH_ALIGN 0x1000
#define CLKCTRL_SPICC_CLK_CNTL (0x8d0)

#define spicc0_clk_sel_fclk_div2 (0 << 9)
#define spicc0_clk_en (1 << 8)
#define spicc0_clk_div(x) (((x)-1) << 0)

namespace clover {
namespace fpbus = fuchsia_hardware_platform_bus;
using spi_channel_t = fidl_metadata::spi::Channel;

static const std::vector<fpbus::Mmio> spi_0_mmios{
    {{
        .base = A1_SPICC0_BASE,
        .length = A1_SPICC0_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> spi_0_irqs{
    {{
        .irq = A1_SPICC0_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    }},
};

static const std::vector<fpbus::Bti> spi_0_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_SPI0,
    }},
};

static constexpr spi_channel_t spi_0_channels[] = {
    {
        .bus_id = CLOVER_SPICC0,
        .cs = 0,  // index into matching chip-select map
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
};

static constexpr amlogic_spi::amlspi_config_t spi_0_config = {
    .capacity = 0,
    .period = 0,
    .bus_id = CLOVER_SPICC0,
    .cs_count = 1,
    .cs = {0},                                      // index into fragments list
    .clock_divider_register_value = (16 >> 1) - 1,  // SCLK = core clock / 16 = 2 MHz
    .use_enhanced_clock_mode = true,                // true  - div_reg = (div >> 1) - 1;
                                                    // false - div_reg = log2(div) - 2;
};

zx_status_t Clover::SpiInit() {
  zx_status_t status;
  constexpr uint32_t kSpiccClkValue =
      // src[10:9]:  0 - fclk_div2(768M)-fixed
      // gate  [8]:  1 - enable clk
      // rate[7:0]: 23 - 768M/(23+1) = 32M
      spicc0_clk_sel_fclk_div2 | spicc0_clk_en | spicc0_clk_div(24);
  {
    // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
    zx::unowned_resource resource(get_root_resource());
    std::optional<fdf::MmioBuffer> buf;
    status = fdf::MmioBuffer::Create(A1_CLK_BASE_ALIGN, A1_CLK_LENGTH_ALIGN, *resource,
                                     ZX_CACHE_POLICY_UNCACHED_DEVICE, &buf);
    if (status != ZX_OK) {
      zxlogf(ERROR, "MmioBuffer::Create failed %s", zx_status_get_string(status));
      return status;
    }

    buf->Write32(kSpiccClkValue, CLKCTRL_SPICC_CLK_CNTL);
  }

  auto spi_gpio = [&arena = gpio_init_arena_](uint64_t alt_function, uint64_t drive_strength_ua)
      -> fuchsia_hardware_gpio_init::wire::GpioInitOptions {
    return fuchsia_hardware_gpio_init::wire::GpioInitOptions::Builder(arena)
        .alt_function(alt_function)
        .drive_strength_ua(drive_strength_ua)
        .Build();
  };

  gpio_init_steps_.push_back({A1_SPI_A_MOSI, spi_gpio(A1_SPI_A_MOSI_FN, 2500)});
  gpio_init_steps_.push_back({A1_SPI_A_MISO, spi_gpio(A1_SPI_A_MISO_FN, 2500)});
  gpio_init_steps_.push_back({A1_SPI_A_CLK, spi_gpio(A1_SPI_A_CLK_FN, 2500)});

  gpio_init_steps_.push_back(
      {A1_SPI_A_SS0, fuchsia_hardware_gpio_init::wire::GpioInitOptions::Builder(gpio_init_arena_)
                         .alt_function(0)  // use gpio chip select here.
                         .output_value(0)
                         .Build()});

  auto spi_status = fidl_metadata::spi::SpiChannelsToFidl(spi_0_channels);
  if (spi_status.is_error()) {
    zxlogf(ERROR, "failed to encode spi channels to fidl: %d", spi_status.error_value());
    return spi_status.error_value();
  }
  auto& data = spi_status.value();

  static const std::vector<fpbus::Metadata> spi_0_metadata{
      {{
          .type = DEVICE_METADATA_AMLSPI_CONFIG,
          .data = std::vector<uint8_t>(
              reinterpret_cast<const uint8_t*>(&spi_0_config),
              reinterpret_cast<const uint8_t*>(&spi_0_config) + sizeof(spi_0_config)),
      }},
      {{
          .type = DEVICE_METADATA_SPI_CHANNELS,
          .data = std::vector<uint8_t>(data.data(), data.data() + data.size()),
      }},
  };

  static const fpbus::Node spi_0_dev = []() {
    fpbus::Node dev = {};
    dev.name() = "spi-0";
    dev.vid() = PDEV_VID_AMLOGIC;
    dev.pid() = PDEV_PID_GENERIC;
    dev.did() = PDEV_DID_AMLOGIC_SPI;
    dev.instance_id() = 0;
    dev.mmio() = spi_0_mmios;
    dev.irq() = spi_0_irqs;
    dev.bti() = spi_0_btis;
    dev.metadata() = spi_0_metadata;
    return dev;
  }();

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('SPI_');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, spi_0_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, spi_0_fragments,
                                               std::size(spi_0_fragments)),
      "pdev");
  if (!result.ok()) {
    zxlogf(ERROR, "AddComposite Spi(spi_0_dev) request failed: %s",
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "AddComposite Spi(spi_0_dev) failed: %s",
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace clover
