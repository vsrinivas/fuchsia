// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fuchsia/hardware/sdmmc/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-a311d/a311d-gpio.h>
#include <soc/aml-a311d/a311d-hw.h>
#include <soc/aml-common/aml-sdmmc.h>

#include "src/devices/board/drivers/vim3/vim3-gpios.h"
#include "src/devices/board/drivers/vim3/vim3-sdio-bind.h"
#include "src/devices/board/drivers/vim3/vim3.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace vim3 {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> sdio_mmios{
    {{
        .base = A311D_EMMC_A_BASE,
        .length = A311D_EMMC_A_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> sdio_irqs{
    {{
        .irq = A311D_SD_EMMC_A_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const std::vector<fpbus::Bti> sdio_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_SDIO,
    }},
};

static aml_sdmmc_config_t config = {
    .supports_dma = true,
    .min_freq = 400'000,
    .max_freq = 200'000'000,
    .version_3 = true,
    .prefs = 0,
};

static const std::vector<fpbus::Metadata> sdio_metadata{
    {{
        .type = DEVICE_METADATA_PRIVATE,
        .data = std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&config),
                                     reinterpret_cast<const uint8_t*>(&config) + sizeof(config)),
    }},
};

zx_status_t Vim3::SdioInit() {
  fpbus::Node sdio_dev;
  sdio_dev.name() = "aml_sdio";
  sdio_dev.vid() = PDEV_VID_AMLOGIC;
  sdio_dev.pid() = PDEV_PID_GENERIC;
  sdio_dev.did() = PDEV_DID_AMLOGIC_SDMMC_A;
  sdio_dev.mmio() = sdio_mmios;
  sdio_dev.irq() = sdio_irqs;
  sdio_dev.bti() = sdio_btis;
  sdio_dev.metadata() = sdio_metadata;

  gpio_impl_.SetAltFunction(A311D_SDIO_D0, A311D_GPIOX_0_SDIO_D0_FN);
  gpio_impl_.SetAltFunction(A311D_SDIO_D1, A311D_GPIOX_1_SDIO_D1_FN);
  gpio_impl_.SetAltFunction(A311D_SDIO_D2, A311D_GPIOX_2_SDIO_D2_FN);
  gpio_impl_.SetAltFunction(A311D_SDIO_D3, A311D_GPIOX_3_SDIO_D3_FN);
  gpio_impl_.SetAltFunction(A311D_SDIO_CLK, A311D_GPIOX_4_SDIO_CLK_FN);
  gpio_impl_.SetAltFunction(A311D_SDIO_CMD, A311D_GPIOX_5_SDIO_CMD_FN);

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('SDIO');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, sdio_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, vim3_sdio_fragments,
                                               std::size(vim3_sdio_fragments)),
      "pdev");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddComposite Sdio(sdio_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddComposite Sdio(sdio_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace vim3
