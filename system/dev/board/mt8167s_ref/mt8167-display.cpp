// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform-bus.h>

#include <soc/mt8167/mt8167-hw.h>
#include "mt8167.h"

namespace board_mt8167 {

zx_status_t Mt8167::DisplayInit() {
    const pbus_mmio_t display_mmios[] = {
        // Overlay
        {
            .base = MT8167_DISP_OVL_BASE,
            .length = MT8167_DISP_OVL_SIZE,
        },
    };

    const pbus_bti_t display_btis[] = {
        {
            .iommu_index = 0,
            .bti_id = BTI_DISPLAY,
        },
    };
    const pbus_irq_t display_irqs[] = {
        {
            .irq  = MT8167_IRQ_DISP_OVL0,
            .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
        },
    };

    pbus_dev_t display_dev = {};
    display_dev.name = "display";
    display_dev.vid = PDEV_VID_MEDIATEK;
    display_dev.pid = PDEV_PID_MEDIATEK_8167S_REF;
    display_dev.did = PDEV_DID_MEDIATEK_DISPLAY;
    display_dev.mmio_list = display_mmios;
    display_dev.mmio_count = countof(display_mmios);
    display_dev.bti_list = display_btis;
    display_dev.bti_count = countof(display_btis);
    display_dev.irq_list = display_irqs;
    display_dev.irq_count = countof(display_irqs);

    zx_status_t status = pbus_.DeviceAdd(&display_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd failed %d\n", __FUNCTION__, status);
        return status;
    }

    return ZX_OK;
}

} // namespace board_mt8167