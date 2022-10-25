// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/syscalls/smc.h>

#include <ddk/metadata/clock.h>
#include <soc/aml-a1/a1-hw.h>
#include <soc/aml-meson/a1-clk.h>

#include "clover.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace clover {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> clk_mmios{
    {{
        .base = A1_CLK_BASE,
        .length = A1_CLK_LENGTH,
    }},
    {{
        .base = A1_ANACTRL_BASE,
        .length = A1_ANACTRL_LENGTH,
    }},
    {{
        .base = A1_MSR_CLK_BASE,
        .length = A1_MSR_CLK_LENGTH,
    }},
};

constexpr clock_id_t clock_ids[] = {
    // place holder
    {a1_clk::CLK_ADC},
};

static const std::vector<fpbus::Metadata> clock_metadata{
    {{
        .type = DEVICE_METADATA_CLOCK_IDS,
        .data =
            std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&clock_ids),
                                 reinterpret_cast<const uint8_t*>(&clock_ids) + sizeof(clock_ids)),
    }},
};

static const std::vector<fpbus::Smc> clk_smcs{
    {{
        .service_call_num_base = ARM_SMC_SERVICE_CALL_NUM_SIP_SERVICE_BASE,
        .count = ARM_SMC_SERVICE_CALL_NUM_SIP_SERVICE_LENGTH,
        .exclusive = false,
    }},
};

static const fpbus::Node clk_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "clover-clk";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_A1;
  dev.did() = PDEV_DID_AMLOGIC_A1_CLK;
  dev.mmio() = clk_mmios;
  dev.metadata() = clock_metadata;
  dev.smc() = clk_smcs;
  return dev;
}();

zx_status_t Clover::ClkInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('CLK_');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, clk_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Clk(clk_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Clk(clk_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace clover
