// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro.h"

namespace astro {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> astro_ram_ctl_mmios{
    {{
        .base = S905D2_DMC_BASE,
        .length = S905D2_DMC_LENGTH,
    }},
};

static const std::vector<fpbus::Bti> astro_ram_ctl_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_RAM_CTL,
    }},
};

static const std::vector<fpbus::Irq> astro_ram_ctl_irqs{
    {{
        .irq = S905D2_DMC_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const fpbus::Node ramctl_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "aml-ram-ctl";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_S905D2;
  dev.did() = PDEV_DID_AMLOGIC_RAM_CTL;
  dev.mmio() = astro_ram_ctl_mmios;
  dev.bti() = astro_ram_ctl_btis;
  dev.irq() = astro_ram_ctl_irqs;
  return dev;
}();

zx_status_t Astro::RamCtlInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('RAMC');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, ramctl_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd RamCtl(ramctl_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd RamCtl(ramctl_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace astro
