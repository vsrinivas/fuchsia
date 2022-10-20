// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/syscalls/smc.h>

#include <soc/aml-a5/a5-hw.h>

#include "av400.h"
#include "src/devices/board/drivers/av400/av400-nna-bind.h"

namespace av400 {

static const std::vector<fpbus::Mmio> nna_mmios{
    {{
        .base = A5_NNA_BASE,
        .length = A5_NNA_LENGTH,
    }},
    // HIU for clocks.
    {{
        .base = A5_CLK_BASE,
        .length = A5_CLK_LENGTH,
    }},
    // Power domain - unused
    {{
        .base = A5_POWER_DOMAIN_BASE,
        .length = A5_POWER_DOMAIN_LENGTH,
    }},
    // Memory PD - unused
    {{
        .base = A5_MEMORY_PD_BASE,
        .length = A5_MEMORY_PD_LENGTH,
    }},
    // AXI SRAM - Temporarily disable
    // According to the actual usage, the space that does not exceed 2M
    //{
    //    .base = A5_NNA_SRAM_BASE,
    //    .length = A5_NNA_SRAM_LENGTH,
    //},
};

static const std::vector<fpbus::Bti> nna_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_NNA,
    }},
};

static const std::vector<fpbus::Irq> nna_irqs{
    {{
        .irq = A5_NNA_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    }},
};

static const std::vector<fpbus::Smc> nna_smcs{
    {{
        .service_call_num_base = ARM_SMC_SERVICE_CALL_NUM_SIP_SERVICE_BASE,
        .count = ARM_SMC_SERVICE_CALL_NUM_SIP_SERVICE_LENGTH,
        .exclusive = false,
    }},
};

static const uint64_t s_external_sram_phys_base = A5_NNA_SRAM_BASE;

static std::vector<fpbus::Metadata> nna_metadata{
    {{
        .type = 0,
        .data = std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&s_external_sram_phys_base),
                                     reinterpret_cast<const uint8_t*>(&s_external_sram_phys_base) +
                                         sizeof(s_external_sram_phys_base)),
    }},
};

static fpbus::Node nna_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "aml-nna";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_A5;
  dev.did() = PDEV_DID_AMLOGIC_NNA;
  dev.mmio() = nna_mmios;
  dev.bti() = nna_btis;
  dev.irq() = nna_irqs;
  dev.metadata() = nna_metadata;
  dev.smc() = nna_smcs;
  return dev;
}();

zx_status_t Av400::NnaInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('NNA_');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, nna_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, nna_fragments, std::size(nna_fragments)),
      "pdev");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: DeviceAdd Nna request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: DeviceAdd Nna failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace av400
