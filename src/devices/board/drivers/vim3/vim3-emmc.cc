// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fuchsia/hardware/sdmmc/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/gpt.h>
#include <soc/aml-a311d/a311d-gpio.h>
#include <soc/aml-a311d/a311d-hw.h>
#include <soc/aml-common/aml-sdmmc.h>

#include "src/devices/board/drivers/vim3/vim3-emmc-bind.h"
#include "src/devices/board/drivers/vim3/vim3.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace vim3 {
namespace fpbus = fuchsia_hardware_platform_bus;
#define BIT_MASK(start, count) (((1 << (count)) - 1) << (start))
#define SET_BITS(dest, start, count, value) \
  ((dest & ~BIT_MASK(start, count)) | (((value) << (start)) & BIT_MASK(start, count)))

static const std::vector<fpbus::Mmio> emmc_mmios{
    {{
        .base = A311D_EMMC_C_BASE,
        .length = A311D_EMMC_C_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> emmc_irqs{
    {{
        .irq = A311D_SD_EMMC_C_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const std::vector<fpbus::Bti> emmc_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_EMMC,
    }},
};

static aml_sdmmc_config_t config = {
    .supports_dma = true,
    .min_freq = 400000,
    .max_freq = 120000000,
    .version_3 = true,
    .prefs = SDMMC_HOST_PREFS_DISABLE_HS400,
};

static const std::vector<fpbus::Metadata> emmc_metadata{
    {{
        .type = DEVICE_METADATA_PRIVATE,
        .data = std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&config),
                                     reinterpret_cast<const uint8_t*>(&config) + sizeof(config)),
    }},
};

static const std::vector<fpbus::BootMetadata> emmc_boot_metadata{
    {{
        .zbi_type = DEVICE_METADATA_PARTITION_MAP,
        .zbi_extra = 0,
    }},
};

zx_status_t Vim3::EmmcInit() {
  fpbus::Node emmc_dev;
  emmc_dev.name() = "aml_emmc";
  emmc_dev.vid() = PDEV_VID_AMLOGIC;
  emmc_dev.pid() = PDEV_PID_GENERIC;
  emmc_dev.did() = PDEV_DID_AMLOGIC_SDMMC_C;
  emmc_dev.mmio() = emmc_mmios;
  emmc_dev.irq() = emmc_irqs;
  emmc_dev.bti() = emmc_btis;
  emmc_dev.metadata() = emmc_metadata;
  emmc_dev.boot_metadata() = emmc_boot_metadata;

  // set alternate functions to enable EMMC
  gpio_impl_.SetAltFunction(A311D_GPIOBOOT(0), A311D_GPIOBOOT_0_EMMC_D0_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOBOOT(1), A311D_GPIOBOOT_1_EMMC_D1_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOBOOT(2), A311D_GPIOBOOT_2_EMMC_D2_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOBOOT(3), A311D_GPIOBOOT_3_EMMC_D3_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOBOOT(4), A311D_GPIOBOOT_4_EMMC_D4_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOBOOT(5), A311D_GPIOBOOT_5_EMMC_D5_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOBOOT(6), A311D_GPIOBOOT_6_EMMC_D6_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOBOOT(7), A311D_GPIOBOOT_7_EMMC_D7_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOBOOT(8), A311D_GPIOBOOT_8_EMMC_CLK_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOBOOT(10), A311D_GPIOBOOT_10_EMMC_CMD_FN);
  // gpio_impl_.SetAltFunction(A311D_GPIOBOOT(12), 1);
  gpio_impl_.SetAltFunction(A311D_GPIOBOOT(13), A311D_GPIOBOOT_13_EMMC_DS_FN);

  gpio_impl_.ConfigOut(A311D_GPIOBOOT(14), 1);

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('EMMC');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, emmc_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, vim3_emmc_fragments,
                                               std::size(vim3_emmc_fragments)),
      "pdev");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddComposite Emmc(emmc_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddComposite Emmc(emmc_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}
}  // namespace vim3
