// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/syscalls/smc.h>

#include "av400.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace av400 {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> av400_dmc_mmios{
    {{
        .base = A5_DMC_BASE,
        .length = A5_DMC_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> av400_dmc_irqs{
    {{
        .irq = A5_DDR_BW_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const std::vector<fpbus::Smc> av400_dmc_smcs{
    {{
        .service_call_num_base = ARM_SMC_SERVICE_CALL_NUM_SIP_SERVICE_BASE,
        .count = ARM_SMC_SERVICE_CALL_NUM_SIP_SERVICE_LENGTH,
        .exclusive = false,
    }},
};

static const fpbus::Node dmc_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "aml-ram-ctl";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_A5;
  dev.did() = PDEV_DID_AMLOGIC_RAM_CTL;
  dev.mmio() = av400_dmc_mmios;
  dev.irq() = av400_dmc_irqs;
  dev.smc() = av400_dmc_smcs;
  return dev;
}();

zx_status_t Av400::DmcInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('DMC_');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, dmc_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Dmc(dmc_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Dmc(dmc_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace av400
