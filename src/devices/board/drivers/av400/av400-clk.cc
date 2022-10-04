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
#include <soc/aml-a5/a5-hw.h>
#include <soc/aml-meson/a5-clk.h>

#include "av400.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace av400 {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> clk_mmios{
    {{
        .base = A5_CLK_BASE,
        .length = A5_CLK_LENGTH,
    }},
    {{
        .base = A5_ANACTRL_BASE,
        .length = A5_ANACTRL_LENGTH,
    }},
    {{
        .base = A5_MSR_CLK_BASE,
        .length = A5_MSR_CLK_LENGTH,
    }},
};

constexpr clock_id_t clock_ids[] = {
    {a5_clk::CLK_ADC},         {a5_clk::CLK_NAND_SEL},       {a5_clk::CLK_PWM_G},
    {a5_clk::CLK_SYS_CPU_CLK}, {a5_clk::CLK_DSPA_PRE_A_SEL}, {a5_clk::CLK_DSPA_PRE_A},
    {a5_clk::CLK_HIFI_PLL},    {a5_clk::CLK_MPLL0},
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
  dev.name() = "av400-clk";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_A5;
  dev.did() = PDEV_DID_AMLOGIC_A5_CLK;
  dev.mmio() = clk_mmios;
  dev.metadata() = clock_metadata;
  dev.smc() = clk_smcs;
  return dev;
}();

zx_status_t Av400::ClkInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('CLK_');
  auto result = pbus_.buffer(arena)->ProtocolNodeAdd(ZX_PROTOCOL_CLOCK_IMPL,
                                                     fidl::ToWire(fidl_arena, clk_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: ProtocolNodeAdd Clk(clk_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: ProtocolNodeAdd Clk(clk_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  clk_impl_ = ddk::ClockImplProtocolClient(parent());
  if (!clk_impl_.is_valid()) {
    zxlogf(ERROR, "ClockImplProtocolClient failed");
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

}  // namespace av400
