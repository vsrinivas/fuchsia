// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <lib/mmio/mmio.h>
#include <hw/reg.h>
#include <lib/zx/handle.h>
#include <soc/aml-common/aml-usb-phy-v2.h>

#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include <optional>

#include "sherlock.h"

namespace sherlock {

namespace {

constexpr pbus_mmio_t xhci_mmios[] = {
    {
        .base = T931_USB0_BASE,
        .length = T931_USB0_LENGTH,
    },
};

constexpr pbus_irq_t xhci_irqs[] = {
    {
        .irq = T931_USB0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

constexpr pbus_bti_t xhci_btis[] = {
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
    dev.mmio_list = xhci_mmios;
    dev.mmio_count = countof(xhci_mmios);
    dev.irq_list = xhci_irqs;
    dev.irq_count = countof(xhci_irqs);
    dev.bti_list = xhci_btis;
    dev.bti_count = countof(xhci_btis);
    return dev;
}();

// magic numbers for USB PHY tuning
constexpr uint32_t PLL_SETTING_3 = 0xfe18;
constexpr uint32_t PLL_SETTING_4 = 0xfff;
constexpr uint32_t PLL_SETTING_5 = 0xc8000;
constexpr uint32_t PLL_SETTING_6 = 0xe0004;
constexpr uint32_t PLL_SETTING_7 = 0xe000c;

zx_status_t PerformUsbTuning(bool host, bool default_val) {
    std::optional<ddk::MmioBuffer> buf;
    zx_status_t status;

    // Please do not use get_root_resource() in new code. See ZX-1467.
    zx::unowned_resource resource(get_root_resource());
    status = ddk::MmioBuffer::Create(T931_USBPHY21_BASE, T931_USBPHY21_LENGTH, *resource,
                                     ZX_CACHE_POLICY_UNCACHED_DEVICE, &buf);
    if (status != ZX_OK) {
        return status;
    }

    if (default_val) {
        buf->Write32(0, 0x38);
        buf->Write32(PLL_SETTING_5, 0x34);
    } else {
        buf->Write32(PLL_SETTING_3, 0x50);
        buf->Write32(PLL_SETTING_4, 0x10);
        if (host) {
            buf->Write32(PLL_SETTING_6, 0x38);
        } else {
            buf->Write32(PLL_SETTING_7, 0x38);
        }
        buf->Write32(PLL_SETTING_5, 0x34);
    }

    return ZX_OK;
}

} // namespace

zx_status_t Sherlock::UsbInit() {
    zx::bti bti;
    auto status = iommu_.GetBti(BTI_BOARD, 0, &bti);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: GetBti failed: %d\n", __func__, status);
        return status;
    }

    status = aml_usb_phy_v2_init(bti.get());
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: aml_usb_phy_v2_init failed %d\n", __func__, status);
        return status;
    }

    status = PerformUsbTuning(true, false);
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
