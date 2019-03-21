// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
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

static const pbus_bti_t vim_video_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_VIDEO,
    },
};

static const pbus_irq_t vim_video_irqs[] = {
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

zx_status_t Vim::VideoInit() {
    pbus_dev_t video_dev = {};
    video_dev.name = "video";
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

    if ((status = pbus_.DeviceAdd(&video_dev)) != ZX_OK) {
        zxlogf(ERROR, "VideoInit: pbus_device_add() failed for video: %d\n", status);
        return status;
    }

    return ZX_OK;
}
} //namespace vim