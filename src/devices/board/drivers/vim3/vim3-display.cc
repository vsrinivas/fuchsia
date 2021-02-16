// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <soc/aml-a311d/a311d-gpio.h>
#include <soc/aml-a311d/a311d-hw.h>

#include "vim3-gpios.h"
#include "vim3.h"

namespace vim3 {

constexpr pbus_mmio_t display_mmios[] = {
    {
        // VBUS/VPU
        .base = A311D_VPU_BASE,
        .length = A311D_VPU_LENGTH,
    },
    {
        // HDMITX
        .base = A311D_HDMITX_BASE,
        .length = A311D_HDMITX_LENGTH,
    },
    {},
    {
        // HHI
        .base = A311D_HIU_BASE,
        .length = A311D_HIU_LENGTH,
    },
    {
        // AOBUS
        .base = A311D_AOBUS_BASE,
        .length = A311D_AOBUS_LENGTH,
    },
    {
        // CBUS
        .base = A311D_CBUS_BASE,
        .length = A311D_CBUS_LENGTH,
    },
};

static const pbus_irq_t display_irqs[] = {
    {
        .irq = A311D_VIU1_VSYNC_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = A311D_RDMA_DONE_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
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
  dev.pid = PDEV_PID_AMLOGIC_A311D;
  dev.did = PDEV_DID_AMLOGIC_DISPLAY;
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

static const zx_bind_inst_t hpd_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, VIM3_HPD_IN),
};

static const zx_bind_inst_t sysmem_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SYSMEM),
};

static const zx_bind_inst_t canvas_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_AMLOGIC_CANVAS),
};

static const device_fragment_part_t hpd_gpio_fragment[] = {
    {countof(root_match), root_match},
    {countof(hpd_gpio_match), hpd_gpio_match},
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
    {"gpio", countof(hpd_gpio_fragment), hpd_gpio_fragment},
    {"sysmem", countof(sysmem_fragment), sysmem_fragment},
    {"canvas", countof(canvas_fragment), canvas_fragment},
};

zx_status_t Vim3::DisplayInit() {
  // TODO(payamm): Change from 1 to UINT32_MAX to separate DSI and Display into two different
  // devhosts once support for it lands.
  auto status = pbus_.CompositeDeviceAdd(&display_dev, reinterpret_cast<uint64_t>(fragments),
                                         countof(fragments), 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd display failed: %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace vim3
