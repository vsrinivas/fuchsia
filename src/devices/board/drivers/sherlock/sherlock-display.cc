// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/display-panel.h>
#include <zircon/errors.h>

#include <ddk/metadata/display.h>
#include <soc/aml-t931/t931-gpio.h>

#include "sherlock-gpios.h"
#include "sherlock.h"
#include "src/devices/board/drivers/sherlock/sherlock-display-bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace sherlock {
namespace fpbus = fuchsia_hardware_platform_bus;

namespace {
static const std::vector<fpbus::Mmio> display_mmios{
    {{
        // VBUS/VPU
        .base = T931_VPU_BASE,
        .length = T931_VPU_LENGTH,
    }},
    {{
        // DSI Host Controller
        .base = T931_TOP_MIPI_DSI_BASE,
        .length = T931_TOP_MIPI_DSI_LENGTH,
    }},
    {{
        // DSI PHY
        .base = T931_DSI_PHY_BASE,
        .length = T931_DSI_PHY_LENGTH,
    }},
    {{
        // HHI
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    }},
    {{
        // AOBUS
        .base = T931_AOBUS_BASE,
        .length = T931_AOBUS_LENGTH,
    }},
    {{
        // CBUS
        .base = T931_CBUS_BASE,
        .length = T931_CBUS_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> display_irqs{
    {{
        .irq = T931_VIU1_VSYNC_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
    {{
        .irq = T931_RDMA_DONE,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
    {{
        .irq = T931_VID1_WR,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const std::vector<fpbus::Bti> display_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_DISPLAY,
    }},
};

}  // namespace

zx_status_t Sherlock::DisplayInit() {
  // Sherlock and Luis have the same display resolution (different size)
  display_panel_t display_panel_info[] = {
      {
          .width = 800,
          .height = 1280,
      },
  };

  if (pid_ == PDEV_PID_SHERLOCK) {
    if (GetDisplayVendor()) {
      display_panel_info[0].panel_type = PANEL_G101B158_FT;
    } else {
      if (GetDdicVersion()) {
        display_panel_info[0].panel_type = PANEL_TV101WXM_FT;
      } else {
        display_panel_info[0].panel_type = PANEL_TV101WXM_FT_9365;
      }
    }
  } else if (pid_ == PDEV_PID_LUIS) {
    display_panel_info[0].panel_type = PANEL_TV080WXM_FT;
  } else {
    zxlogf(ERROR, "%s: Unsupported board detected: pid = %u\n", __func__, pid_);
    return ZX_ERR_NOT_SUPPORTED;
  }

  std::vector<fpbus::Metadata> display_panel_metadata{
      {{
          .type = DEVICE_METADATA_DISPLAY_CONFIG,
          .data = std::vector<uint8_t>(
              reinterpret_cast<uint8_t*>(&display_panel_info),
              reinterpret_cast<uint8_t*>(&display_panel_info) + sizeof(display_panel_info)),
      }},
  };

  static const fpbus::Node display_dev = [&]() {
    fpbus::Node dev = {};
    dev.name() = "display";
    dev.vid() = PDEV_VID_AMLOGIC;
    dev.pid() = PDEV_PID_AMLOGIC_S905D2;
    dev.did() = PDEV_DID_AMLOGIC_DISPLAY;
    dev.metadata() = std::move(display_panel_metadata);
    dev.mmio() = display_mmios;
    dev.irq() = display_irqs;
    dev.bti() = display_btis;
    return dev;
  }();

  // TODO(payamm): Change from "dsi" to nullptr to separate DSI and Display into two different
  // driver hosts once support has landed for it
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('DISP');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, display_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, display_fragments,
                                               std::size(display_fragments)),
      "dsi");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddComposite Display(display_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddComposite Display(display_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace sherlock
