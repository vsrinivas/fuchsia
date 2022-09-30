// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-common/aml-registers.h>
#include <soc/aml-s905d3/s905d3-hw.h>

#include "nelson.h"
#include "src/devices/board/drivers/nelson/nelson_aml_nna_bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace nelson {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> nna_mmios{
    {{
        .base = S905D3_NNA_BASE,
        .length = S905D3_NNA_LENGTH,
    }},
    {{
        .base = S905D3_HIU_BASE,
        .length = S905D3_HIU_LENGTH,
    }},
    {{
        .base = S905D3_POWER_DOMAIN_BASE,
        .length = S905D3_POWER_DOMAIN_LENGTH,
    }},
    {{
        .base = S905D3_MEMORY_PD_BASE,
        .length = S905D3_MEMORY_PD_LENGTH,
    }},
};

static const std::vector<fpbus::Bti> nna_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_NNA,
    }},
};

static const std::vector<fpbus::Irq> nna_irqs{
    {{
        .irq = S905D3_NNA_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    }},
};

static const fpbus::Node nna_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "aml-nna";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_S905D3;
  dev.did() = PDEV_DID_AMLOGIC_NNA;
  dev.mmio() = nna_mmios;
  dev.bti() = nna_btis;
  dev.irq() = nna_irqs;
  return dev;
}();

zx_status_t Nelson::NnaInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('NNA_');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, nna_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, aml_nna_fragments,
                                               std::size(aml_nna_fragments)),
      "pdev");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddComposite Nna(nna_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddComposite Nna(nna_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace nelson
