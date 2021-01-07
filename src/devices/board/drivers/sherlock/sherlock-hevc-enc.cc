// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <soc/aml-meson/g12b-clk.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"

namespace sherlock {

static pbus_mmio_t sherlock_hevc_enc_mmios[] = {
    {
        .base = T931_CBUS_BASE,
        .length = T931_CBUS_LENGTH,
    },
    {
        .base = T931_DOS_BASE,
        .length = T931_DOS_LENGTH,
    },
    {
        .base = T931_AOBUS_BASE,
        .length = T931_AOBUS_LENGTH,
    },
    {
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    },
    {
        .base = T931_WAVE420L_BASE,
        .length = T931_WAVE420L_LENGTH,
    },
};

constexpr pbus_bti_t sherlock_hevc_enc_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_HEVC_ENC,
    },
};

constexpr pbus_irq_t sherlock_hevc_enc_irqs[] = {
    {
        .irq = T931_WAVE420L_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
constexpr zx_bind_inst_t sysmem_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SYSMEM),
};
const zx_bind_inst_t clk_dos_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, g12b_clk::G12B_CLK_DOS),
};
constexpr device_fragment_part_t sysmem_fragment[] = {
    {countof(root_match), root_match},
    {countof(sysmem_match), sysmem_match},
};
constexpr device_fragment_part_t clk_dos_fragment[] = {
    {countof(root_match), root_match},
    {countof(clk_dos_match), clk_dos_match},
};
constexpr device_fragment_t fragments[] = {
    {"sysmem", countof(sysmem_fragment), sysmem_fragment},
    {"clock-dos", countof(clk_dos_fragment), clk_dos_fragment},
};

static pbus_dev_t hevc_enc_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-hevc-enc";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_T931;
  dev.did = PDEV_DID_AMLOGIC_HEVC_ENC;
  dev.mmio_list = sherlock_hevc_enc_mmios;
  dev.mmio_count = countof(sherlock_hevc_enc_mmios);
  dev.bti_list = sherlock_hevc_enc_btis;
  dev.bti_count = countof(sherlock_hevc_enc_btis);
  dev.irq_list = sherlock_hevc_enc_irqs;
  dev.irq_count = countof(sherlock_hevc_enc_irqs);
  return dev;
}();

zx_status_t Sherlock::HevcEncInit() {
  zx_status_t status =
      pbus_.CompositeDeviceAdd(&hevc_enc_dev, fragments, countof(fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Sherlock::HevcEncInit: CompositeDeviceAdd() failed: %d", status);
    return status;
  }
  return ZX_OK;
}

}  // namespace sherlock
