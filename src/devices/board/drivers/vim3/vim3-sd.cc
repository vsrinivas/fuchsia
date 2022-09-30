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

#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"
#include "vim3.h"

namespace vim3 {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> sd_mmios{
    {{
        .base = A311D_EMMC_B_BASE,
        .length = A311D_EMMC_B_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> sd_irqs{
    {{
        .irq = A311D_SD_EMMC_B_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const std::vector<fpbus::Bti> sd_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_SD,
    }},
};

static aml_sdmmc_config_t config = {
    .supports_dma = true,
    .min_freq = 400'000,
    .max_freq = 50'000'000,
    .version_3 = true,
    .prefs = 0,
};

static const std::vector<fpbus::Metadata> sd_metadata{
    {{
        .type = DEVICE_METADATA_PRIVATE,
        .data = std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&config),
                                     reinterpret_cast<const uint8_t*>(&config) + sizeof(config)),
    }},
};

zx_status_t Vim3::SdInit() {
  fpbus::Node sd_dev;
  sd_dev.name() = "aml_sd";
  sd_dev.vid() = PDEV_VID_AMLOGIC;
  sd_dev.pid() = PDEV_PID_GENERIC;
  sd_dev.did() = PDEV_DID_AMLOGIC_SDMMC_B;
  sd_dev.mmio() = sd_mmios;
  sd_dev.irq() = sd_irqs;
  sd_dev.bti() = sd_btis;
  sd_dev.metadata() = sd_metadata;

  gpio_impl_.SetAltFunction(A311D_GPIOC(0), A311D_GPIOC_0_SDCARD_D0_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOC(1), A311D_GPIOC_1_SDCARD_D1_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOC(2), A311D_GPIOC_2_SDCARD_D2_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOC(3), A311D_GPIOC_3_SDCARD_D3_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOC(4), A311D_GPIOC_4_SDCARD_CLK_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOC(5), A311D_GPIOC_5_SDCARD_CMD_FN);

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('SD__');
  auto result = pbus_.buffer(arena)->AddCompositeImplicitPbusFragment(
      fidl::ToWire(fidl_arena, sd_dev), {}, {});
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Sd(sd_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Sd(sd_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace vim3
