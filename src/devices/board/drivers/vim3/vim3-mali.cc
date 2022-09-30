// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-a311d/a311d-hw.h>
#include <soc/aml-common/aml-registers.h>

#include "src/devices/board/drivers/vim3/vim3-mali-bind.h"
#include "src/devices/board/drivers/vim3/vim3.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace vim3 {
namespace fpbus = fuchsia_hardware_platform_bus;
static const std::vector<fpbus::Mmio> mali_mmios{
    {{
        .base = A311D_MALI_BASE,
        .length = A311D_MALI_LENGTH,
    }},
    {{
        .base = A311D_HIU_BASE,
        .length = A311D_HIU_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> mali_irqs{
    {{
        .irq = A311D_MALI_IRQ_PP,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    }},
    {{
        .irq = A311D_MALI_IRQ_GPMMU,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    }},
    {{
        .irq = A311D_MALI_IRQ_GP,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    }},
};

static const std::vector<fpbus::Bti> mali_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_MALI,
    }},
};

static const fpbus::Node mali_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "mali";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_A311D;
  dev.did() = PDEV_DID_AMLOGIC_MALI_INIT;
  dev.mmio() = mali_mmios;
  dev.irq() = mali_irqs;
  dev.bti() = mali_btis;
  return dev;
}();

zx_status_t Vim3::MaliInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('MALI');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, mali_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, vim3_mali_fragments,
                                               std::size(vim3_mali_fragments)),
      "pdev");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddComposite Mali(mali_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddComposite Mali(mali_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace vim3
