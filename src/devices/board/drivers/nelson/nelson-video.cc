// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/syscalls/smc.h>

#include <soc/aml-meson/sm1-clk.h>
#include <soc/aml-s905d3/s905d3-hw.h>

#include "nelson.h"
#include "src/devices/board/drivers/nelson/nelson_aml_video_bind.h"

namespace nelson {

constexpr pbus_mmio_t nelson_video_mmios[] = {
    {
        .base = S905D3_CBUS_BASE,
        .length = S905D3_CBUS_LENGTH,
    },
    {
        .base = S905D3_DOS_BASE,
        .length = S905D3_DOS_LENGTH,
    },
    {
        .base = S905D3_HIU_BASE,
        .length = S905D3_HIU_LENGTH,
    },
    {
        .base = S905D3_AOBUS_BASE,
        .length = S905D3_AOBUS_LENGTH,
    },
    {
        .base = S905D3_DMC_BASE,
        .length = S905D3_DMC_LENGTH,
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
        .irq = S905D3_DEMUX_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D3_PARSER_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D3_DOS_MBOX_0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D3_DOS_MBOX_1_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D3_DOS_MBOX_2_IRQ,
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

constexpr pbus_dev_t video_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-video";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_S905D3;
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
  if ((status = pbus_.AddComposite(&video_dev, reinterpret_cast<uint64_t>(aml_video_fragments),
                                   countof(aml_video_fragments), "pdev")) != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd() failed: %d", __func__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace nelson
