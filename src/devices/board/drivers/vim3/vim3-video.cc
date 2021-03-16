// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-a311d/a311d-hw.h>
#include <soc/aml-meson/g12b-clk.h>

#include "vim3.h"

namespace vim3 {

static constexpr pbus_mmio_t vim_video_mmios[] = {
    {
        .base = A311D_FULL_CBUS_BASE,
        .length = A311D_FULL_CBUS_LENGTH,
    },
    {
        .base = A311D_DOS_BASE,
        .length = A311D_DOS_LENGTH,
    },
    {
        .base = A311D_HIU_BASE,
        .length = A311D_HIU_LENGTH,
    },
    {
        .base = A311D_AOBUS_BASE,
        .length = A311D_AOBUS_LENGTH,
    },
    {
        .base = A311D_DMC_BASE,
        .length = A311D_DMC_LENGTH,
    },
};

static constexpr pbus_bti_t vim_video_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_VIDEO,
    },
};

static constexpr pbus_irq_t vim_video_irqs[] = {
    {
        .irq = A311D_DEMUX_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = A311D_PARSER_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = A311D_DOS_MBOX_0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = A311D_DOS_MBOX_1_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = A311D_DOS_MBOX_2_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static constexpr zx_bind_inst_t sysmem_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SYSMEM),
};
static constexpr zx_bind_inst_t canvas_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_AMLOGIC_CANVAS),
};
static constexpr zx_bind_inst_t dos_gclk0_vdec_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, g12b_clk::G12B_CLK_DOS_GCLK_VDEC),
};
static constexpr zx_bind_inst_t clk_dos_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, g12b_clk::G12B_CLK_DOS),
};
static constexpr device_fragment_part_t sysmem_fragment[] = {
    {countof(root_match), root_match},
    {countof(sysmem_match), sysmem_match},
};
static constexpr device_fragment_part_t canvas_fragment[] = {
    {countof(root_match), root_match},
    {countof(canvas_match), canvas_match},
};
static constexpr device_fragment_part_t dos_gclk0_vdec_fragment[] = {
    {countof(root_match), root_match},
    {countof(dos_gclk0_vdec_match), dos_gclk0_vdec_match},
};
static constexpr device_fragment_part_t clk_dos_fragment[] = {
    {countof(root_match), root_match},
    {countof(clk_dos_match), clk_dos_match},
};
static constexpr device_fragment_t fragments[] = {
    {"sysmem", countof(sysmem_fragment), sysmem_fragment},
    {"canvas", countof(canvas_fragment), canvas_fragment},
    {"clock-dos-vdec", countof(dos_gclk0_vdec_fragment), dos_gclk0_vdec_fragment},
    {"clock-dos", countof(clk_dos_fragment), clk_dos_fragment},
};

zx_status_t Vim3::VideoInit() {
  pbus_dev_t video_dev = {};
  video_dev.name = "aml-video";
  video_dev.vid = PDEV_VID_AMLOGIC;
  video_dev.pid = PDEV_PID_AMLOGIC_A311D;
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
}  // namespace vim3
