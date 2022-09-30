// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/syscalls/smc.h>

#include <soc/aml-meson/g12a-clk.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro.h"
#include "src/devices/board/drivers/astro/astro-video-bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace astro {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> astro_video_mmios{
    {{
        .base = S905D2_CBUS_BASE,
        .length = S905D2_CBUS_LENGTH,
    }},
    {{
        .base = S905D2_DOS_BASE,
        .length = S905D2_DOS_LENGTH,
    }},
    {{
        .base = S905D2_HIU_BASE,
        .length = S905D2_HIU_LENGTH,
    }},
    {{
        .base = S905D2_AOBUS_BASE,
        .length = S905D2_AOBUS_LENGTH,
    }},
    {{
        .base = S905D2_DMC_BASE,
        .length = S905D2_DMC_LENGTH,
    }},
};

static const std::vector<fpbus::Bti> astro_video_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_VIDEO,
    }},
};

static const std::vector<fpbus::Irq> astro_video_irqs{
    {{
        .irq = S905D2_DEMUX_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
    {{
        .irq = S905D2_PARSER_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
    {{
        .irq = S905D2_DOS_MBOX_0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
    {{
        .irq = S905D2_DOS_MBOX_1_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
    {{
        .irq = S905D2_DOS_MBOX_2_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const std::vector<fpbus::Smc> astro_video_smcs{
    {{
        .service_call_num_base = ARM_SMC_SERVICE_CALL_NUM_TRUSTED_OS_BASE,
        .count = 1,
        .exclusive = false,
    }},
};

static const fpbus::Node video_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "aml-video";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_S905D2;
  dev.did() = PDEV_DID_AMLOGIC_VIDEO;
  dev.mmio() = astro_video_mmios;
  dev.bti() = astro_video_btis;
  dev.irq() = astro_video_irqs;
  dev.smc() = astro_video_smcs;
  return dev;
}();

zx_status_t Astro::VideoInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('VIDE');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, video_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, aml_video_fragments,
                                               std::size(aml_video_fragments)),
      "pdev");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddComposite Video(video_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddComposite Video(video_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace astro
