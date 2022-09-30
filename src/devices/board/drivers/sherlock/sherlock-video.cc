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

#include <soc/aml-meson/g12b-clk.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"
#include "src/devices/board/drivers/sherlock/sherlock-video-bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace sherlock {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> sherlock_video_mmios{
    {{
        .base = T931_CBUS_BASE,
        .length = T931_CBUS_LENGTH,
    }},
    {{
        .base = T931_DOS_BASE,
        .length = T931_DOS_LENGTH,
    }},
    {{
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    }},
    {{
        .base = T931_AOBUS_BASE,
        .length = T931_AOBUS_LENGTH,
    }},
    {{
        .base = T931_DMC_BASE,
        .length = T931_DMC_LENGTH,
    }},
};

static const std::vector<fpbus::Bti> sherlock_video_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_VIDEO,
    }},
};

static const std::vector<fpbus::Irq> sherlock_video_irqs{
    {{
        .irq = T931_DEMUX_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
    {{
        .irq = T931_PARSER_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
    {{
        .irq = T931_DOS_MBOX_0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
    {{
        .irq = T931_DOS_MBOX_1_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const std::vector<fpbus::Smc> sherlock_video_smcs{
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
  dev.pid() = PDEV_PID_AMLOGIC_T931;
  dev.did() = PDEV_DID_AMLOGIC_VIDEO;
  dev.mmio() = sherlock_video_mmios;
  dev.bti() = sherlock_video_btis;
  dev.irq() = sherlock_video_irqs;
  dev.smc() = sherlock_video_smcs;
  return dev;
}();

zx_status_t Sherlock::VideoInit() {
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

}  // namespace sherlock
