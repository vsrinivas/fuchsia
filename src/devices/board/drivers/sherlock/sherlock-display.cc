// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/display-panel.h>
#include <zircon/errors.h>

#include <lib/ddk/metadata.h>
#include <ddk/metadata/display.h>
#include <soc/aml-t931/t931-gpio.h>

#include "sherlock-gpios.h"
#include "sherlock.h"

namespace sherlock {

namespace {
constexpr pbus_mmio_t display_mmios[] = {
    {
        // VBUS/VPU
        .base = T931_VPU_BASE,
        .length = T931_VPU_LENGTH,
    },
    {
        // DSI Host Controller
        .base = T931_TOP_MIPI_DSI_BASE,
        .length = T931_TOP_MIPI_DSI_LENGTH,
    },
    {
        // DSI PHY
        .base = T931_DSI_PHY_BASE,
        .length = T931_DSI_PHY_LENGTH,
    },
    {
        // HHI
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    },
    {
        // AOBUS
        .base = T931_AOBUS_BASE,
        .length = T931_AOBUS_LENGTH,
    },
    {
        // CBUS
        .base = T931_CBUS_BASE,
        .length = T931_CBUS_LENGTH,
    },
};

static const pbus_irq_t display_irqs[] = {
    {
        .irq = T931_VIU1_VSYNC_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = T931_RDMA_DONE,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = T931_VID1_WR,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

pbus_metadata_t display_panel_metadata[] = {
    {
        .type = DEVICE_METADATA_DISPLAY_CONFIG,
        .data_buffer = nullptr,
        .data_size = 0,
    },
};

static const pbus_bti_t display_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_DISPLAY,
    },
};

static pbus_dev_t display_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "display";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_S905D2;
  dev.did = PDEV_DID_AMLOGIC_DISPLAY;
  dev.metadata_list = display_panel_metadata;
  dev.metadata_count = countof(display_panel_metadata);
  dev.mmio_list = display_mmios;
  dev.mmio_count = countof(display_mmios);
  dev.irq_list = display_irqs;
  dev.irq_count = countof(display_irqs);
  dev.bti_list = display_btis;
  dev.bti_count = countof(display_btis);
  return dev;
}();

// Composite binding rules for display driver.
static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t dsi_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_DSI_IMPL),
};
static const zx_bind_inst_t lcd_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_LCD_RESET),
};
static const zx_bind_inst_t sysmem_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SYSMEM),
};
static const zx_bind_inst_t canvas_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_AMLOGIC_CANVAS),
};
static const device_fragment_part_t dsi_fragment[] = {
    {countof(root_match), root_match},
    {countof(dsi_match), dsi_match},
};
static const device_fragment_part_t lcd_gpio_fragment[] = {
    {countof(root_match), root_match},
    {countof(lcd_gpio_match), lcd_gpio_match},
};
static const device_fragment_part_t sysmem_fragment[] = {
    {countof(root_match), root_match},
    {countof(sysmem_match), sysmem_match},
};
static const device_fragment_part_t canvas_fragment[] = {
    {countof(root_match), root_match},
    {countof(canvas_match), canvas_match},
};
static const device_fragment_t fragments[] = {
    {"dsi", countof(dsi_fragment), dsi_fragment},
    {"gpio-lcd", countof(lcd_gpio_fragment), lcd_gpio_fragment},
    {"sysmem", countof(sysmem_fragment), sysmem_fragment},
    {"canvas", countof(canvas_fragment), canvas_fragment},
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
    uint8_t pt;
    gpio_impl_.ConfigIn(GPIO_PANEL_DETECT, GPIO_NO_PULL);
    gpio_impl_.Read(GPIO_PANEL_DETECT, &pt);
    if (pt) {
      display_panel_info[0].panel_type = PANEL_G101B158_FT;
    } else {
      gpio_impl_.ConfigIn(GPIO_DDIC_DETECT, GPIO_NO_PULL);
      gpio_impl_.Read(GPIO_DDIC_DETECT, &pt);
      if (pt == 1) {
        display_panel_info[0].panel_type = PANEL_TV101WXM_FT;
      } else {
        display_panel_info[0].panel_type = PANEL_TV101WXM_FT_9365;
      }
    }
    display_panel_metadata[0].data_size = sizeof(display_panel_info);
    display_panel_metadata[0].data_buffer = reinterpret_cast<uint8_t*>(&display_panel_info);
  } else if (pid_ == PDEV_PID_LUIS) {
    display_panel_info[0].panel_type = PANEL_TV080WXM_FT;
    display_panel_metadata[0].data_size = sizeof(display_panel_info);
    display_panel_metadata[0].data_buffer = reinterpret_cast<uint8_t*>(&display_panel_info);
  } else {
    zxlogf(ERROR, "%s: Unsupported board detected: pid = %u\n", __func__, pid_);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // TODO(payamm): Change from 1 to UINT32_MAX to separate DSI and Display into two different
  // driver hosts once support has landed for it
  auto status = pbus_.CompositeDeviceAdd(&display_dev, reinterpret_cast<uint64_t>(fragments),
                                         countof(fragments), 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed: %d", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace sherlock
