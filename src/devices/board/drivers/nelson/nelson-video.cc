// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls/smc.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/aml-meson/sm1-clk.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "nelson.h"

namespace nelson {

constexpr pbus_mmio_t nelson_video_mmios[] = {
    {
        .base = S905D2_CBUS_BASE,
        .length = S905D2_CBUS_LENGTH,
    },
    {
        .base = S905D2_DOS_BASE,
        .length = S905D2_DOS_LENGTH,
    },
    {
        .base = S905D2_HIU_BASE,
        .length = S905D2_HIU_LENGTH,
    },
    {
        .base = S905D2_AOBUS_BASE,
        .length = S905D2_AOBUS_LENGTH,
    },
    {
        .base = S905D2_DMC_BASE,
        .length = S905D2_DMC_LENGTH,
    },
};

constexpr pbus_bti_t nelson_video_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_VIDEO,
    },
};

constexpr pbus_irq_t nelson_video_irqs[] = {
    {
        .irq = S905D2_DEMUX_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_PARSER_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_DOS_MBOX_0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_DOS_MBOX_1_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_DOS_MBOX_2_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

constexpr pbus_smc_t nelson_video_smcs[] = {
    {
        .service_call_num_base = ARM_SMC_SERVICE_CALL_NUM_TRUSTED_OS_BASE,
        .count = 1,
        .exclusive = false,
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
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, sm1_clk::CLK_DOS_GCLK_VDEC),
};
const zx_bind_inst_t clk_dos_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, sm1_clk::CLK_DOS),
};
constexpr zx_bind_inst_t tee_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_TEE),
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
constexpr device_fragment_part_t tee_fragment[] = {
    {countof(root_match), root_match},
    {countof(tee_match), tee_match},
};
constexpr device_fragment_t fragments[] = {
    {"sysmem", countof(sysmem_fragment), sysmem_fragment},
    {"canvas", countof(canvas_fragment), canvas_fragment},
    {"clock-doc-vdec", countof(dos_gclk0_vdec_fragment), dos_gclk0_vdec_fragment},
    {"clock-dos", countof(clk_dos_fragment), clk_dos_fragment},
    {"tee", countof(tee_fragment), tee_fragment},
};

constexpr pbus_dev_t video_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-video";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_S905D2;
  dev.did = PDEV_DID_AMLOGIC_VIDEO;
  dev.mmio_list = nelson_video_mmios;
  dev.mmio_count = countof(nelson_video_mmios);
  dev.bti_list = nelson_video_btis;
  dev.bti_count = countof(nelson_video_btis);
  dev.irq_list = nelson_video_irqs;
  dev.irq_count = countof(nelson_video_irqs);
  dev.smc_list = nelson_video_smcs;
  dev.smc_count = countof(nelson_video_smcs);
  return dev;
}();

zx_status_t Nelson::VideoInit() {
  zx_status_t status;
  if ((status = pbus_.CompositeDeviceAdd(&video_dev, fragments, countof(fragments), UINT32_MAX)) !=
      ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd() failed: %d", __func__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace nelson
