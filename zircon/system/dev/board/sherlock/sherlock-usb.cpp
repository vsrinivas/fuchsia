// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>

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

constexpr pbus_mmio_t usb_phy_mmios[] = {
    {
        .base = T931_RESET_BASE,
        .length = T931_RESET_LENGTH,
    },
    {
        .base = T931_USBCTRL_BASE,
        .length = T931_USBCTRL_LENGTH,
    },
    {
        .base = T931_USBPHY20_BASE,
        .length = T931_USBPHY20_LENGTH,
    },
    {
        .base = T931_USBPHY21_BASE,
        .length = T931_USBPHY21_LENGTH,
    },
};

constexpr pbus_bti_t usb_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_USB,
    },
};

static pbus_dev_t xhci_dev = [](){
    pbus_dev_t dev;
    dev.name = "xhci";
    dev.vid = PDEV_VID_GENERIC;
    dev.pid = PDEV_PID_GENERIC;
    dev.did = PDEV_DID_USB_XHCI_COMPOSITE;
    dev.mmio_list = xhci_mmios;
    dev.mmio_count = countof(xhci_mmios);
    dev.irq_list = xhci_irqs;
    dev.irq_count = countof(xhci_irqs);
    dev.bti_list = usb_btis;
    dev.bti_count = countof(usb_btis);
    return dev;
}();

// values from mesong12b.dtsi usb2_phy_v2 pll-setting-#
constexpr uint32_t pll_settings[] = {
    0x09400414,
    0x927E0000,
    0xac5f69e5,
    0xfe18,
    0x8000fff,
    0x78000,
    0xe0004,
    0xe000c,
};

static const pbus_metadata_t usb_phy_metadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = pll_settings,
        .data_size = sizeof(pll_settings),
    },
};

static const pbus_dev_t usb_phy_dev = [](){
    pbus_dev_t dev;
    dev.name = "aml-usb-phy-v2";
    dev.vid = PDEV_VID_AMLOGIC;
    dev.did = PDEV_DID_AML_USB_PHY_V2;
    dev.mmio_list = usb_phy_mmios;
    dev.mmio_count = countof(usb_phy_mmios);
    dev.bti_list = usb_btis;
    dev.bti_count = countof(usb_btis);
    dev.metadata_list = usb_phy_metadata;
    dev.metadata_count = countof(usb_phy_metadata);
    return dev;
}();

static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t usb_phy_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_USB_PHY),
};
static const device_component_part_t usb_phy_component[] = {
    { countof(root_match), root_match },
    { countof(usb_phy_match), usb_phy_match },
};
static const device_component_t components[] = {
    { countof(usb_phy_component), usb_phy_component },
};

} // namespace

zx_status_t Sherlock::UsbInit() {
    auto status = pbus_.DeviceAdd(&usb_phy_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd failed %d\n", __func__, status);
        return status;
    }

    // Add XHCI to same devhost as the aml-usb-phy driver.
    return pbus_.CompositeDeviceAdd(&xhci_dev, components, countof(components), 1);
}

} // namespace sherlock
