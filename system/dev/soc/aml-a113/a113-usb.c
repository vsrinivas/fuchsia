// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/protocol/platform-defs.h>
#include <hw/reg.h>

#include "a113-bus.h"
#include "a113-usb-phy.h"

#define DWC3_MMIO_BASE      0xff500000
#define DWC3_MMIO_LENGTH    0x100000
#define DWC3_IRQ            62

#define BIT_MASK(start, count) (((1 << (count)) - 1) << (start))
#define SET_BITS(dest, start, count, value) \
        ((dest & ~BIT_MASK(start, count)) | (((value) << (start)) & BIT_MASK(start, count)))


static const pbus_mmio_t dwc3_mmios[] = {
    {
        .base = DWC3_MMIO_BASE,
        .length = DWC3_MMIO_LENGTH,
    },
};

static const pbus_irq_t dwc3_irqs[] = {
    {
        .irq = DWC3_IRQ,
    },
};

static const pbus_dev_t dwc3_dev = {
    .name = "dwc3",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_USB_DWC3,
    .mmios = dwc3_mmios,
    .mmio_count = countof(dwc3_mmios),
    .irqs = dwc3_irqs,
    .irq_count = countof(dwc3_irqs),
};

static const pbus_dev_t xhci_dev = {
    .name = "dwc3-xhci",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_USB_XHCI,
    .mmios = dwc3_mmios,
    .mmio_count = countof(dwc3_mmios),
    .irqs = dwc3_irqs,
    .irq_count = countof(dwc3_irqs),
};

zx_status_t a113_usb_init(a113_bus_t* bus) {
    zx_status_t status;

    status = io_buffer_init_physical(&bus->usb_phy, 0xffe09000, 4096, get_root_resource(),
                                     ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        printf("a113_usb_init io_buffer_init_physical failed %d\n", status);
        return status;
    }
    volatile void* regs = io_buffer_virt(&bus->usb_phy);

    // amlogic_new_usb2_init
    for (int i = 0; i < 4; i++) {
        volatile void* addr = regs + (i * PHY_REGISTER_SIZE) + U2P_R0_OFFSET;
        uint32_t temp = readl(addr);
        temp |= U2P_R0_POR;
        temp |= U2P_R0_DMPULLDOWN;
        temp |= U2P_R0_DPPULLDOWN;
        if (i == 1) {
            temp |= U2P_R0_IDPULLUP;
        }
        writel(temp, addr);
        zx_nanosleep(zx_deadline_after(ZX_USEC(500)));
        temp = readl(addr);
        temp &= ~U2P_R0_POR;
        writel(temp, addr);
    }

    // amlogic_new_usb3_init
    volatile void* addr = regs + (4 * PHY_REGISTER_SIZE);

    uint32_t temp = readl(addr + USB_R1_OFFSET);
    temp = SET_BITS(temp, USB_R1_U3H_FLADJ_30MHZ_REG_START, USB_R1_U3H_FLADJ_30MHZ_REG_BITS, 0x20);
    writel(temp, addr + USB_R1_OFFSET);

    temp = readl(addr + USB_R5_OFFSET);
    temp |= USB_R5_IDDIG_EN0;
    temp |= USB_R5_IDDIG_EN1;
    temp = SET_BITS(temp, USB_R5_IDDIG_TH_START, USB_R5_IDDIG_TH_BITS, 255);
    writel(temp, addr + USB_R5_OFFSET);

    // add dwc3 device
    if ((status = pbus_device_add(&bus->pbus, &dwc3_dev, 0)) != ZX_OK) {
        dprintf(ERROR, "a113_usb_init could not add dwc3_dev: %d\n", status);
        return status;
    }
    // xhci_dev is enabled/disabled dynamically, so don't enable it here
    if ((status = pbus_device_add(&bus->pbus, &xhci_dev, PDEV_ADD_DISABLED)) != ZX_OK) {
        dprintf(ERROR, "a113_usb_init could not add xhci_dev: %d\n", status);
        return status;
    }

    return ZX_OK;
}

zx_status_t a113_usb_set_mode(a113_bus_t* bus, usb_mode_t mode) {
    // TODO(voydanoff) more work will be needed here for switching to peripheral mode

    // add or remove XHCI device
    pbus_device_enable(&bus->pbus, PDEV_VID_GENERIC, PDEV_PID_GENERIC, PDEV_DID_USB_XHCI,
                       mode == USB_MODE_HOST);
    return ZX_OK;
}
