// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-meson/axg-clk.h>
#include <soc/aml-s912/s912-hw.h>

#include "vim.h"

namespace vim {
static pbus_mmio_t vim_video_mmios[] = {
    {
        .base = S912_FULL_CBUS_BASE,
        .length = S912_FULL_CBUS_LENGTH,
    },
    {
        .base = S912_DOS_BASE,
        .length = S912_DOS_LENGTH,
    },
    {
        .base = S912_HIU_BASE,
        .length = S912_HIU_LENGTH,
    },
    {
        .base = S912_AOBUS_BASE,
        .length = S912_AOBUS_LENGTH,
    },
    {
        .base = S912_DMC_REG_BASE,
        .length = S912_DMC_REG_LENGTH,
    },
};

constexpr pbus_bti_t vim_video_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_VIDEO,
    },
};

constexpr pbus_irq_t vim_video_irqs[] = {
    {
        .irq = S912_DEMUX_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S912_PARSER_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S912_DOS_MBOX_0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S912_DOS_MBOX_1_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S912_DOS_MBOX_2_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
constexpr zx_bind_inst_t sysmem_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SYSMEM),
};
constexpr zx_bind_inst_t canvas_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_AMLOGIC_CANVAS),
};
const zx_bind_inst_t dos_gclk0_vdec_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, axg_clk::CLK_DOS_GCLK_VDEC),
};
const zx_bind_inst_t clk_dos_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, axg_clk::CLK_AXG_DOS),
};
constexpr device_fragment_part_t sysmem_fragment[] = {
    {countof(root_match), root_match},
    {countof(sysmem_match), sysmem_match},
};
constexpr device_fragment_part_t canvas_fragment[] = {
    {countof(root_match), root_match},
    {countof(canvas_match), canvas_match},
};
constexpr device_fragment_part_t dos_gclk0_vdec_fragment[] = {
    {countof(root_match), root_match},
    {countof(dos_gclk0_vdec_match), dos_gclk0_vdec_match},
};
constexpr device_fragment_part_t clk_dos_fragment[] = {
    {countof(root_match), root_match},
    {countof(clk_dos_match), clk_dos_match},
};
constexpr device_fragment_t fragments[] = {
    {"sysmem", countof(sysmem_fragment), sysmem_fragment},
    {"canvas", countof(canvas_fragment), canvas_fragment},
    {"clock-dos-vdec", countof(dos_gclk0_vdec_fragment), dos_gclk0_vdec_fragment},
    {"clock-dos", countof(clk_dos_fragment), clk_dos_fragment},
};

zx_status_t Vim::VideoInit() {
  pbus_dev_t video_dev = {};
  video_dev.name = "aml-video";
  video_dev.vid = PDEV_VID_AMLOGIC;
  video_dev.pid = PDEV_PID_AMLOGIC_S912;
  video_dev.did = PDEV_DID_AMLOGIC_VIDEO;
  video_dev.mmio_list = vim_video_mmios;
  video_dev.mmio_count = countof(vim_video_mmios);
  video_dev.irq_list = vim_video_irqs;
  video_dev.irq_count = countof(vim_video_irqs);
  video_dev.bti_list = vim_video_btis;
  video_dev.bti_count = countof(vim_video_btis);

  zx_status_t status;

  if ((status = pbus_.CompositeDeviceAdd(&video_dev, reinterpret_cast<uint64_t>(fragments),
                                         countof(fragments), UINT32_MAX)) != ZX_OK) {
    zxlogf(ERROR, "VideoInit: CompositeDeviceAdd() failed for video: %d", status);
    return status;
  }

  return ZX_OK;
}
}  // namespace vim
