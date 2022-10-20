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
#include <soc/aml-common/aml-spi.h>

#include "buckeye-gpios.h"
#include "buckeye.h"
#include "src/devices/board/drivers/buckeye/spi_0_bind.h"
#include "src/devices/board/drivers/buckeye/spi_1_bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"
#include "src/devices/lib/fidl-metadata/spi.h"

#define CLKCTRL_SPICC_CLK_CNTL (0x5d * 4)
#define spicc1_clk_sel_fclk_div2 (4 << 23)
#define spicc1_clk_en (1 << 22)
#define spicc1_clk_div(x) (((x)-1) << 16)
#define spicc0_clk_sel_fclk_div2 (4 << 7)
#define spicc0_clk_en (1 << 6)
#define spicc0_clk_div(x) (((x)-1) << 0)

namespace buckeye {
namespace fpbus = fuchsia_hardware_platform_bus;
using spi_channel_t = fidl_metadata::spi::Channel;

static const std::vector<fpbus::Mmio> spi_1_mmios{
    {{
        .base = A5_SPICC1_BASE,
        .length = A5_SPICC1_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> spi_1_irqs{
    {{
        .irq = A5_SPICC1_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    }},
};

static const std::vector<fpbus::Bti> spi_1_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_SPI1,
    }},
};

static constexpr spi_channel_t spi_1_channels[] = {
    {
        .bus_id = BUCKEYE_SPICC1,
        .cs = 0,  // index into matching chip-select map
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
};

static constexpr amlogic_spi::amlspi_config_t spi_1_config = {
    .capacity = 0,
    .period = 0,
    .bus_id = BUCKEYE_SPICC1,
    .cs_count = 1,
    .cs = {0},                                     // index into fragments list
    .clock_divider_register_value = (2 >> 1) - 1,  // SCLK = core clock (32.26MHz) / 2 = 16.18 MHz
    .use_enhanced_clock_mode = true,               // true  - div_reg = (div >> 1) - 1;
                                                   // false - div_reg = log2(div) - 2;
};

static fpbus::Node spi_1_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "spi-1";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_GENERIC;
  dev.did() = PDEV_DID_AMLOGIC_SPI;
  dev.instance_id() = 0;
  dev.mmio() = spi_1_mmios;
  dev.irq() = spi_1_irqs;
  dev.bti() = spi_1_btis;
  return dev;
}();

static const std::vector<fpbus::Mmio> spi_0_mmios{
    {{
        .base = A5_SPICC0_BASE,
        .length = A5_SPICC0_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> spi_0_irqs{
    {{
        .irq = A5_SPICC0_IRQ,
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
        .bus_id = BUCKEYE_SPICC0,
        .cs = 0,  // index into matching chip-select map
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
};

static constexpr amlogic_spi::amlspi_config_t spi_0_config = {
    .capacity = 0,
    .period = 0,
    .bus_id = BUCKEYE_SPICC0,
    .cs_count = 1,
    .cs = {0},                                      // index into fragments list
    .clock_divider_register_value = (40 >> 1) - 1,  // SCLK = core clock / 40 = 1 MHz
    .use_enhanced_clock_mode = true,                // true  - div_reg = (div >> 1) - 1;
                                                    // false - div_reg = log2(div) - 2;
};

static fpbus::Node spi_0_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "spi-0";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_GENERIC;
  dev.did() = PDEV_DID_AMLOGIC_SPI;
  dev.instance_id() = 1;
  dev.mmio() = spi_0_mmios;
  dev.irq() = spi_0_irqs;
  dev.bti() = spi_0_btis;
  return dev;
}();

zx_status_t Buckeye::SpiInit() {
  zx_status_t status;
  constexpr uint32_t kSpiccClkValue =
      // src [25:23]:  4 - fclk_div2(1000M)-fixed
      // gate   [22]:  1 - enable clk
      // rate[21:16]: 24 - 1000M/(31) ~= 32.26M
      spicc1_clk_sel_fclk_div2 | spicc1_clk_en | spicc1_clk_div(31) |
      // src [9:7]:  4 - fclk_div2(1000M)-fixed
      // gate  [6]:  1 - enable clk
      // rate[5:0]: 24 - 1000M/(24+1) = 40M
      spicc0_clk_sel_fclk_div2 | spicc0_clk_en | spicc0_clk_div(25);
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

  // setup pinmux for SPICC0 bus arbiter.
  // SS0: use gpio chip select here.
  gpio_impl_.SetAltFunction(A5_GPIOT(4), A5_GPIOT_4_SPI_A_SS0_FN);  // SS0
  gpio_impl_.ConfigOut(A5_GPIOT(4), 1);

  gpio_impl_.SetAltFunction(A5_GPIOT(1), A5_GPIOT_1_SPI_A_SCLK_FN);  // SCLK
  gpio_impl_.SetDriveStrength(A5_GPIOT(1), 2500, nullptr);

  gpio_impl_.SetAltFunction(A5_GPIOT(2), A5_GPIOT_2_SPI_A_MOSI_FN);  // MOSI
  gpio_impl_.SetDriveStrength(A5_GPIOT(2), 2500, nullptr);

  gpio_impl_.SetAltFunction(A5_GPIOT(3), A5_GPIOT_3_SPI_A_MISO_FN);  // MISO
  gpio_impl_.SetDriveStrength(A5_GPIOT(3), 2500, nullptr);

  // setup pinmux for SPICC1 bus arbiter.
  // SS0: use gpio chip select here.
  gpio_impl_.SetAltFunction(A5_GPIOT(10), A5_GPIOT_10_SPI_B_SS0_FN);  // SS0
  gpio_impl_.ConfigOut(A5_GPIOT(10), 1);

  gpio_impl_.SetAltFunction(A5_GPIOT(11), A5_GPIOT_11_SPI_B_SCLK_FN);  // SCLK
  gpio_impl_.SetDriveStrength(A5_GPIOT(11), 2500, nullptr);

  gpio_impl_.SetAltFunction(A5_GPIOT(12), A5_GPIOT_12_SPI_B_MOSI_FN);  // MOSI
  gpio_impl_.SetDriveStrength(A5_GPIOT(12), 2500, nullptr);

  gpio_impl_.SetAltFunction(A5_GPIOT(13), A5_GPIOT_13_SPI_B_MISO_FN);  // MISO
  gpio_impl_.SetDriveStrength(A5_GPIOT(13), 2500, nullptr);

  {
    std::vector<fpbus::Metadata> spi_1_metadata;
    spi_1_metadata.emplace_back([]() {
      fpbus::Metadata ret;
      ret.type() = DEVICE_METADATA_AMLSPI_CONFIG,
      ret.data() = std::vector<uint8_t>(
          reinterpret_cast<const uint8_t*>(&spi_1_config),
          reinterpret_cast<const uint8_t*>(&spi_1_config) + sizeof(spi_1_config));
      return ret;
    }());
    auto spi_status = fidl_metadata::spi::SpiChannelsToFidl(spi_1_channels);
    if (spi_status.is_error()) {
      zxlogf(ERROR, "%s: failed to encode spi channels to fidl: %d", __func__,
             spi_status.error_value());
      return spi_status.error_value();
    }
    auto& data = spi_status.value();

    spi_1_metadata.emplace_back([&]() {
      fpbus::Metadata ret;
      ret.type() = DEVICE_METADATA_SPI_CHANNELS;
      ret.data() = std::move(data);
      return ret;
    }());

    spi_1_dev.metadata() = std::move(spi_1_metadata);

    fidl::Arena<> fidl_arena;
    fdf::Arena arena('SPI_');
    auto result = pbus_.buffer(arena)->AddComposite(
        fidl::ToWire(fidl_arena, spi_1_dev),
        platform_bus_composite::MakeFidlFragment(fidl_arena, spi_1_fragments,
                                                 std::size(spi_1_fragments)),
        "pdev");
    if (!result.ok()) {
      zxlogf(ERROR, "%s: AddComposite Spi(spi_1_dev) request failed: %s", __func__,
             result.FormatDescription().data());
      return result.status();
    }
    if (result->is_error()) {
      zxlogf(ERROR, "%s: AddComposite Spi(spi_1_dev) failed: %s", __func__,
             zx_status_get_string(result->error_value()));
      return result->error_value();
    }
  }

  {
    std::vector<fpbus::Metadata> spi_0_metadata;
    spi_0_metadata.emplace_back([]() {
      fpbus::Metadata ret;
      ret.type() = DEVICE_METADATA_AMLSPI_CONFIG,
      ret.data() = std::vector<uint8_t>(
          reinterpret_cast<const uint8_t*>(&spi_0_config),
          reinterpret_cast<const uint8_t*>(&spi_0_config) + sizeof(spi_0_config));
      return ret;
    }());

    auto spi_status = fidl_metadata::spi::SpiChannelsToFidl(spi_0_channels);
    if (spi_status.is_error()) {
      zxlogf(ERROR, "%s: failed to encode spi channels to fidl: %d", __func__,
             spi_status.error_value());
      return spi_status.error_value();
    }
    auto& data = spi_status.value();

    spi_0_metadata.emplace_back([&]() {
      fpbus::Metadata ret;
      ret.type() = DEVICE_METADATA_SPI_CHANNELS;
      ret.data() = std::move(data);
      return ret;
    }());

    spi_0_dev.metadata() = std::move(spi_0_metadata);

    fidl::Arena<> fidl_arena;
    fdf::Arena arena('SPI_');
    auto result = pbus_.buffer(arena)->AddComposite(
        fidl::ToWire(fidl_arena, spi_0_dev),
        platform_bus_composite::MakeFidlFragment(fidl_arena, spi_0_fragments,
                                                 std::size(spi_0_fragments)),
        "pdev");
    if (!result.ok()) {
      zxlogf(ERROR, "%s: AddComposite Spi(spi_0_dev) request failed: %s", __func__,
             result.FormatDescription().data());
      return result.status();
    }
    if (result->is_error()) {
      zxlogf(ERROR, "%s: AddComposite Spi(spi_0_dev) failed: %s", __func__,
             zx_status_get_string(result->error_value()));
      return result->error_value();
    }
  }
  return ZX_OK;
}

}  // namespace buckeye
