// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/usb/modeswitch.h>
#include <soc/hi3660/hi3660-hw.h>

#include <stdio.h>

#include "hikey960.h"
#include "hikey960-hw.h"

static const pbus_mmio_t dwc3_mmios[] = {
    {
        .base = MMIO_USB3OTG_BASE,
        .length = MMIO_USB3OTG_LENGTH,
    },
};

static const pbus_irq_t dwc3_irqs[] = {
    {
        .irq = IRQ_USB3,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_bti_t dwc3_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_USB_DWC3,
    },
};

static usb_mode_t dwc3_mode = USB_MODE_HOST;

static const pbus_metadata_t dwc2_metadata[] = {
    {
        .type        = DEVICE_METADATA_USB_MODE,
        .data_buffer = &dwc3_mode,
        .data_size   = sizeof(dwc3_mode),
    }
};

static const pbus_dev_t hikey_usb_children[] = {
    {
        .name = "dwc3",
        .vid = PDEV_VID_GENERIC,
        .pid = PDEV_PID_GENERIC,
        .did = PDEV_DID_USB_DWC3,
        .mmio_list = dwc3_mmios,
        .mmio_count = countof(dwc3_mmios),
        .irq_list = dwc3_irqs,
        .irq_count = countof(dwc3_irqs),
        .bti_list = dwc3_btis,
        .bti_count = countof(dwc3_btis),
        .metadata_list = dwc2_metadata,
        .metadata_count = countof(dwc2_metadata),
    },
};

static const pbus_gpio_t hikey_usb_gpios[] = {
    {
        .gpio = GPIO_HUB_VDD33_EN,
    },
    {
        .gpio = GPIO_VBUS_TYPEC,
    },
    {
        .gpio = GPIO_USBSW_SW_SEL,
    },
};

const pbus_dev_t hikey_usb_dev = {
    .name = "hikey-usb",
    .vid = PDEV_VID_96BOARDS,
    .pid = PDEV_PID_HIKEY960,
    .did = PDEV_DID_HIKEY960_USB,
    .gpio_list = hikey_usb_gpios,
    .gpio_count = countof(hikey_usb_gpios),
    .child_list = hikey_usb_children,
    .child_count = countof(hikey_usb_children),
};

zx_status_t hikey960_usb_init(hikey960_t* hikey) {
    zx_status_t status;

    if ((status = pbus_device_add(&hikey->pbus, &hikey_usb_dev)) != ZX_OK) {
        zxlogf(ERROR, "hikey960_add_devices could not add hikey_usb_dev: %d\n", status);
        return status;
    }

    return ZX_OK;
}
