// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mmio/mmio.h>

#include <soc/as370/as370-hw.h>

#include "pinecrest-gpio.h"
#include "pinecrest.h"
#include "src/devices/board/drivers/pinecrest/pinecrest-emmc-bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace board_pinecrest {
namespace fpbus = fuchsia_hardware_platform_bus;

zx_status_t Pinecrest::EmmcInit() {
  static const std::vector<fpbus::Mmio> emmc_mmios{
      {{
          .base = as370::kEmmc0Base,
          .length = as370::kEmmc0Size,
      }},
  };

  static const std::vector<fpbus::Irq> emmc_irqs{
      {{
          .irq = as370::kEmmc0Irq,
          .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
      }},
  };

  static const std::vector<fpbus::Bti> emmc_btis{
      {{
          .iommu_index = 0,
          .bti_id = BTI_EMMC0,
      }},
  };

  fpbus::Node emmc_dev;
  emmc_dev.name() = "pinecrest-emmc";
  emmc_dev.vid() = PDEV_VID_SYNAPTICS;
  emmc_dev.pid() = PDEV_PID_SYNAPTICS_AS370;
  emmc_dev.did() = PDEV_DID_AS370_SDHCI1;
  emmc_dev.irq() = emmc_irqs;
  emmc_dev.mmio() = emmc_mmios;
  emmc_dev.bti() = emmc_btis;

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('EMMC');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, emmc_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, pinecrest_emmc_fragments,
                                               std::size(pinecrest_emmc_fragments)),
      "pdev");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Emmc(emmc_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Emmc(emmc_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace board_pinecrest
