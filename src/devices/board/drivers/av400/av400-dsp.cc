// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mmio/mmio.h>
#include <zircon/syscalls/smc.h>

#include <soc/aml-a5/a5-hw.h>

#include "av400.h"
#include "src/devices/board/drivers/av400/av400-dsp-bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace av400 {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> dsp_mmios{
    {{
        .base = A5_DSPA_BASE,
        .length = A5_DSPA_BASE_LENGTH,
    }},
    {{
        .base = A5_DSP_SRAM_BASE,
        .length = A5_DSP_SRAM_BASE_LENGTH,
    }},
};

static const std::vector<fpbus::Smc> dsp_smcs{
    {{
        .service_call_num_base = ARM_SMC_SERVICE_CALL_NUM_SIP_SERVICE_BASE,
        .count = ARM_SMC_SERVICE_CALL_NUM_SIP_SERVICE_LENGTH,
        .exclusive = false,
    }},
};

static const fpbus::Node dsp_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "dsp";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_A5;
  dev.did() = PDEV_DID_AMLOGIC_DSP;
  dev.mmio() = dsp_mmios;
  dev.smc() = dsp_smcs;
  return dev;
}();

zx_status_t Av400::DspInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('DSP_');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, dsp_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, av400_dsp_fragments,
                                               std::size(av400_dsp_fragments)),
      "pdev");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddComposite Dsp(dsp_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddComposite Dsp(dsp_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace av400
