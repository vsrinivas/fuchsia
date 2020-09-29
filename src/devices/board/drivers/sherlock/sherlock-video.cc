// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls/smc.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/aml-meson/g12b-clk.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"

namespace sherlock {

static pbus_mmio_t sherlock_video_mmios[] = {
    {
        .base = T931_CBUS_BASE,
        .length = T931_CBUS_LENGTH,
    },
    {
        .base = T931_DOS_BASE,
        .length = T931_DOS_LENGTH,
    },
    {
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    },
    {
        .base = T931_AOBUS_BASE,
        .length = T931_AOBUS_LENGTH,
    },
    {
        .base = T931_DMC_BASE,
        .length = T931_DMC_LENGTH,
    },
};

constexpr pbus_bti_t sherlock_video_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_VIDEO,
    },
};

constexpr pbus_irq_t sherlock_video_irqs[] = {
    {
        .irq = T931_DEMUX_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = T931_PARSER_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = T931_DOS_MBOX_0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = T931_DOS_MBOX_1_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

constexpr pbus_smc_t sherlock_video_smcs[] = {
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
constexpr zx_bind_inst_t tee_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_TEE),
};
const zx_bind_inst_t dos_gclk0_vdec_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, g12b_clk::G12B_CLK_DOS_GCLK_VDEC),
};
const zx_bind_inst_t clk_dos_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, g12b_clk::G12B_CLK_DOS),
};
constexpr device_fragment_part_t sysmem_fragment[] = {
    {countof(root_match), root_match},
    {countof(sysmem_match), sysmem_match},
};
constexpr device_fragment_part_t canvas_fragment[] = {
    {countof(root_match), root_match},
    {countof(canvas_match), canvas_match},
};
constexpr device_fragment_part_t tee_fragment[] = {
    {countof(root_match), root_match},
    {countof(tee_match), tee_match},
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
    {countof(sysmem_fragment), sysmem_fragment},
    {countof(canvas_fragment), canvas_fragment},
    {countof(dos_gclk0_vdec_fragment), dos_gclk0_vdec_fragment},
    {countof(clk_dos_fragment), clk_dos_fragment},
    {countof(tee_fragment), tee_fragment},
};

static pbus_dev_t video_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-video";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_T931;
  dev.did = PDEV_DID_AMLOGIC_VIDEO;
  dev.mmio_list = sherlock_video_mmios;
  dev.mmio_count = countof(sherlock_video_mmios);
  dev.bti_list = sherlock_video_btis;
  dev.bti_count = countof(sherlock_video_btis);
  dev.irq_list = sherlock_video_irqs;
  dev.irq_count = countof(sherlock_video_irqs);
  dev.smc_list = sherlock_video_smcs;
  dev.smc_count = countof(sherlock_video_smcs);
  return dev;
}();

zx_status_t Sherlock::VideoInit() {
  zx_status_t status =
      pbus_.CompositeDeviceAdd(&video_dev, fragments, countof(fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Sherlock::VideoInit: CompositeDeviceAdd() failed for video: %d", status);
    return status;
  }
  return ZX_OK;
}

}  // namespace sherlock
