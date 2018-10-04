// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/platform-defs.h>
#include <hw/reg.h>
#include <lib/zx/handle.h>
#include <soc/aml-common/aml-usb-phy-v2.h>

#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"

namespace sherlock {

static const pbus_mmio_t xhci_mmios[] = {
    {
        .base = T931_USB0_BASE,
        .length = T931_USB0_LENGTH,
    },
};

static const pbus_irq_t xhci_irqs[] = {
    {
        .irq = T931_USB0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_bti_t xhci_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_USB_XHCI,
    },
};

static pbus_dev_t xhci_dev = [](){
    pbus_dev_t dev;
    dev.name = "xhci";
    dev.vid = PDEV_VID_GENERIC;
    dev.pid = PDEV_PID_GENERIC;
    dev.did = PDEV_DID_USB_XHCI;
    dev.mmios = xhci_mmios;
    dev.mmio_count = countof(xhci_mmios);
    dev.irqs = xhci_irqs;
    dev.irq_count = countof(xhci_irqs);
    dev.btis = xhci_btis;
    dev.bti_count = countof(xhci_btis);
    return dev;
}();

// magic numbers for USB PHY tuning
#define PLL_SETTING_3   0xfe18
#define PLL_SETTING_4   0xfff
#define PLL_SETTING_5   0xc8000
#define PLL_SETTING_6   0xe0004
#define PLL_SETTING_7   0xe000c

#define PHY_WRITE(value, base, offset) writel((value), \
                reinterpret_cast<volatile uint32_t*>(static_cast<uint8_t*>(base) + offset))

static zx_status_t perform_usb_tuning(zx_handle_t bti, bool host, bool default_val) {
    io_buffer_t buf;
    zx_status_t status;

    status = io_buffer_init_physical(&buf, bti, T931_USBPHY21_BASE, T931_USBPHY21_LENGTH,
                                     get_root_resource(), ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        return status;
    }

    auto* base = io_buffer_virt(&buf);

    if (default_val) {
        PHY_WRITE(0, base, 0x38);
        PHY_WRITE(PLL_SETTING_5, base, 0x34);
    } else {
        PHY_WRITE(PLL_SETTING_3, base, 0x50);
        PHY_WRITE(PLL_SETTING_4, base, 0x10);
        if (host) {
            PHY_WRITE(PLL_SETTING_6, base, 0x38);
        } else {
            PHY_WRITE(PLL_SETTING_7, base, 0x38);
        }
        PHY_WRITE(PLL_SETTING_5, base, 0x34);
    }

    io_buffer_release(&buf);
    return ZX_OK;
}

zx_status_t Sherlock::UsbInit() {
    zx::handle bti;
    auto status = iommu_.GetBti(BTI_BOARD, 0, bti.reset_and_get_address());
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: GetBti failed: %d\n", __func__, status);
        return status;
    }

    status = aml_usb_phy_v2_init(bti.get());
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: aml_usb_phy_v2_init failed %d\n", __func__, status);
        return status;
    }

    status = perform_usb_tuning(bti.get(), true, false);
    if (status != ZX_OK) {
        return status;
    }

    status = pbus_.DeviceAdd(&xhci_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd failed %d\n", __func__, status);
        return status;
    }

    return ZX_OK;
}

} // namespace sherlock
