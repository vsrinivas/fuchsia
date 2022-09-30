// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-a311d/a311d-hw.h>
#include <soc/aml-meson/g12b-clk.h>

#include "src/devices/board/drivers/vim3/vim3-video-bind.h"
#include "src/devices/board/drivers/vim3/vim3.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace vim3 {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> vim_video_mmios{
    {{
        .base = A311D_FULL_CBUS_BASE,
        .length = A311D_FULL_CBUS_LENGTH,
    }},
    {{
        .base = A311D_DOS_BASE,
        .length = A311D_DOS_LENGTH,
    }},
    {{
        .base = A311D_HIU_BASE,
        .length = A311D_HIU_LENGTH,
    }},
    {{
        .base = A311D_AOBUS_BASE,
        .length = A311D_AOBUS_LENGTH,
    }},
    {{
        .base = A311D_DMC_BASE,
        .length = A311D_DMC_LENGTH,
    }},
};

static const std::vector<fpbus::Bti> vim_video_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_VIDEO,
    }},
};

static const std::vector<fpbus::Irq> vim_video_irqs{
    {{
        .irq = A311D_DEMUX_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
    {{
        .irq = A311D_PARSER_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
    {{
        .irq = A311D_DOS_MBOX_0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
    {{
        .irq = A311D_DOS_MBOX_1_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

zx_status_t Vim3::VideoInit() {
  fpbus::Node video_dev;
  video_dev.name() = "aml-video";
  video_dev.vid() = PDEV_VID_AMLOGIC;
  video_dev.pid() = PDEV_PID_AMLOGIC_A311D;
  video_dev.did() = PDEV_DID_AMLOGIC_VIDEO;
  video_dev.mmio() = vim_video_mmios;
  video_dev.irq() = vim_video_irqs;
  video_dev.bti() = vim_video_btis;

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('VIDE');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, video_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, vim3_video_fragments,
                                               std::size(vim3_video_fragments)),
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
}  // namespace vim3
