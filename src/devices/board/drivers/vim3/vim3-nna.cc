// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-a311d/a311d-hw.h>
#include <soc/aml-common/aml-registers.h>

#include "src/devices/board/drivers/vim3/vim3-nna-bind.h"
#include "src/devices/board/drivers/vim3/vim3.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace vim3 {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> vim3_nna_mmios{
    {{
        .base = A311D_NNA_BASE,
        .length = A311D_NNA_LENGTH,
    }},
    {{
        .base = A311D_HIU_BASE,
        .length = A311D_HIU_LENGTH,
    }},
    {{
        .base = A311D_POWER_DOMAIN_BASE,
        .length = A311D_POWER_DOMAIN_LENGTH,
    }},
    {{
        .base = A311D_MEMORY_PD_BASE,
        .length = A311D_MEMORY_PD_LENGTH,
    }},
    {{
        .base = A311D_NNA_SRAM_BASE,
        .length = A311D_NNA_SRAM_LENGTH,
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
        .irq = A311D_NNA_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    }},
};

static uint64_t s_external_sram_phys_base = A311D_NNA_SRAM_BASE;

static const std::vector<fpbus::Metadata> nna_metadata{
    {{
        .type = 0,
        .data = std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&s_external_sram_phys_base),
                                     reinterpret_cast<const uint8_t*>(&s_external_sram_phys_base) +
                                         sizeof(s_external_sram_phys_base)),
    }},
};

static const fpbus::Node nna_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "aml-nna";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_A311D;
  dev.did() = PDEV_DID_AMLOGIC_NNA;
  dev.mmio() = vim3_nna_mmios;
  dev.bti() = nna_btis;
  dev.irq() = nna_irqs;
  dev.metadata() = nna_metadata;
  return dev;
}();

zx_status_t Vim3::NnaInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('NNA_');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, nna_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, vim3_nna_fragments,
                                               std::size(vim3_nna_fragments)),
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

}  // namespace vim3
