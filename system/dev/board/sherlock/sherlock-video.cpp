// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sherlock.h"

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform-bus.h>
#include <soc/aml-t931/t931-hw.h>

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

static const pbus_bti_t sherlock_video_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_VIDEO,
    },
};

static const pbus_irq_t sherlock_video_irqs[] = {
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
    {
        .irq = T931_DOS_MBOX_2_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const uint32_t sherlock_video_protocols[] = {
    ZX_PROTOCOL_AMLOGIC_CANVAS,
};

static pbus_dev_t video_dev = []() {
    pbus_dev_t dev;
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
    dev.protocol_list = sherlock_video_protocols;
    dev.protocol_count = countof(sherlock_video_protocols);
    return dev;
}();

zx_status_t Sherlock::VideoInit() {
    zx_status_t status = pbus_.DeviceAdd(&video_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Sherlock::VideoInit: pbus_device_add() failed for video: %d\n", status);
        return status;
    }
    return ZX_OK;
}

} // namespace sherlock
