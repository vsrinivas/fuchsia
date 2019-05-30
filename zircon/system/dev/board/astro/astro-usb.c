// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/mmio-buffer.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <hw/reg.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro.h"

static const pbus_mmio_t xhci_mmios[] = {
    {
        .base = S905D2_USB0_BASE,
        .length = S905D2_USB0_LENGTH,
    },
};

static const pbus_irq_t xhci_irqs[] = {
    {
        .irq = S905D2_USB0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_bti_t usb_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_USB,
    },
};

static const pbus_dev_t xhci_dev = {
    .name = "xhci",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_USB_XHCI_COMPOSITE,
    .mmio_list = xhci_mmios,
    .mmio_count = countof(xhci_mmios),
    .irq_list = xhci_irqs,
    .irq_count = countof(xhci_irqs),
    .bti_list = usb_btis,
    .bti_count = countof(usb_btis),
};

static const pbus_mmio_t usb_phy_mmios[] = {
    {
        .base = S905D2_RESET_BASE,
        .length = S905D2_RESET_LENGTH,
    },
    {
        .base = S905D2_USBCTRL_BASE,
        .length = S905D2_USBCTRL_LENGTH,
    },
    {
        .base = S905D2_USBPHY20_BASE,
        .length = S905D2_USBPHY20_LENGTH,
    },
    {
        .base = S905D2_USBPHY21_BASE,
        .length = S905D2_USBPHY21_LENGTH,
    },
};

// values from mesong12b.dtsi usb2_phy_v2 pll-setting-#
static const uint32_t pll_settings[] = {
    0x09400414,
    0x927E0000,
    0xac5f49e5,
    0xfe18,
    0xfff,
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

static const pbus_dev_t usb_phy_dev = {
    .name = "aml-usb-phy-v2",
    .vid = PDEV_VID_AMLOGIC,
    .did = PDEV_DID_AML_USB_PHY_V2,
    .mmio_list = usb_phy_mmios,
    .mmio_count = countof(usb_phy_mmios),
    .bti_list = usb_btis,
    .bti_count = countof(usb_btis),
    .metadata_list = usb_phy_metadata,
    .metadata_count = countof(usb_phy_metadata),
};

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

zx_status_t aml_usb_init(aml_bus_t* bus) {
    zx_status_t status = pbus_device_add(&bus->pbus, &usb_phy_dev);
    if (status != ZX_OK) {
        return status;
    }

    // Add XHCI to same devhost as the aml-usb-phy driver.
    return pbus_composite_device_add(&bus->pbus, &xhci_dev, components, countof(components), 1);
}
