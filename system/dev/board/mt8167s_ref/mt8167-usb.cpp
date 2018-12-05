// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/mmio-buffer.h>
#include <ddk/platform-defs.h>
#include <hw/reg.h>
#include <lib/zx/handle.h>

#include <soc/mt8167/mt8167-hw.h>

#include "mt8167.h"

namespace board_mt8167 {

static const pbus_mmio_t usb_mmios[] = {
    {
        .base = MT8167_USB0_BASE,
        .length = MT8167_USB0_LENGTH,
    },
    {
        .base = MT8167_USBPHY_BASE,
        .length = MT8167_USBPHY_LENGTH,
    },
};

static const pbus_irq_t usb_irqs[] = {
    {
        .irq = MT8167_IRQ_USB_MCU,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
};

static const pbus_bti_t usb_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_USB,
    },
};

static pbus_dev_t usb_dev = [](){
    pbus_dev_t dev;
    dev.name = "mt-usb";
    dev.vid = PDEV_VID_MEDIATEK;
    dev.did = PDEV_DID_MUSB_PERIPHERAL;
    dev.mmio_list = usb_mmios;
    dev.mmio_count = countof(usb_mmios);
    dev.irq_list = usb_irqs;
    dev.irq_count = countof(usb_irqs);
    dev.bti_list = usb_btis;
    dev.bti_count = countof(usb_btis);
    return dev;
}();

#define CLK_GATING_CTRL1_CLR (0x084 / sizeof(uint32_t))
#define SET_USB_SW_CG (1U << 13)

zx_status_t Mt8167::UsbInit() {
    // TODO: move to clock driver when we have one
    mmio_buffer_t buf;
    zx_status_t status;

    status = mmio_buffer_init_physical(&buf, MT8167_XO_BASE, MT8167_XO_SIZE,
                                       get_root_resource(), ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        return status;
    }

    volatile uint32_t* xo_base = static_cast<uint32_t*>(buf.vaddr);

    writel(SET_USB_SW_CG, xo_base + CLK_GATING_CTRL1_CLR);

    mmio_buffer_release(&buf);

    status = pbus_.DeviceAdd(&usb_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd failed %d\n", __func__, status);
        return status;
    }

    return ZX_OK;
}

} // namespace board_mt8167
