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

#include "src/devices/board/drivers/vim3/vim3-video-bind.h"
#include "src/devices/board/drivers/vim3/vim3.h"

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
};

zx_status_t Vim3::VideoInit() {
  pbus_dev_t video_dev = {};
  video_dev.name = "aml-video";
  video_dev.vid = PDEV_VID_AMLOGIC;
  video_dev.pid = PDEV_PID_AMLOGIC_A311D;
  video_dev.did = PDEV_DID_AMLOGIC_VIDEO;
  video_dev.mmio_list = vim_video_mmios;
  video_dev.mmio_count = std::size(vim_video_mmios);
  video_dev.irq_list = vim_video_irqs;
  video_dev.irq_count = std::size(vim_video_irqs);
  video_dev.bti_list = vim_video_btis;
  video_dev.bti_count = std::size(vim_video_btis);

  zx_status_t status;

  if ((status = pbus_.AddComposite(&video_dev, reinterpret_cast<uint64_t>(vim3_video_fragments),
                                   std::size(vim3_video_fragments), "pdev")) != ZX_OK) {
    zxlogf(ERROR, "VideoInit: CompositeDeviceAdd() failed for video: %d", status);
    return status;
  }

  return ZX_OK;
}
}  // namespace vim3
