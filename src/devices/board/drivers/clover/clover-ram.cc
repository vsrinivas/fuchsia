// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-a1/a1-hw.h>

#include "clover.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace clover {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> clover_dmc_mmios{
    {{
        .base = A1_DMC_BASE,
        .length = A1_DMC_LENGTH,
    }},
    {{
        .base = A1_CLK_BASE,
        .length = A1_CLK_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> clover_dmc_irqs{
    {{
        .irq = A1_DDR_BW_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const fpbus::Node dmc_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "aml-ram-ctl";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_A1;
  dev.did() = PDEV_DID_AMLOGIC_RAM_CTL;
  dev.mmio() = clover_dmc_mmios;
  dev.irq() = clover_dmc_irqs;
  return dev;
}();

zx_status_t Clover::DmcInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('DMC_');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, dmc_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "NodeAdd Dmc(dmc_dev) request failed: %s", result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "NodeAdd Dmc(dmc_dev) failed: %s", zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace clover
