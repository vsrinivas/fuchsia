// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/protocol/platform-defs.h>
#include <hw/reg.h>

#include <soc/aml-common/aml-usb-phy.h>
#include <soc/aml-a113/a113-hw.h>

#include "gauss.h"
#include "gauss-hw.h"

#define BIT_MASK(start, count) (((1 << (count)) - 1) << (start))
#define SET_BITS(dest, start, count, value) \
        ((dest & ~BIT_MASK(start, count)) | (((value) << (start)) & BIT_MASK(start, count)))

static const pbus_mmio_t xhci_mmios[] = {
    {
        .base = DWC3_MMIO_BASE,
        .length = DWC3_MMIO_LENGTH,
    },
};

static const pbus_irq_t xhci_irqs[] = {
    {
        .irq = DWC3_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_bti_t xhci_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_USB_XHCI,
    },
};

static const pbus_dev_t xhci_dev = {
    .name = "xhci",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_USB_XHCI,
    .mmios = xhci_mmios,
    .mmio_count = countof(xhci_mmios),
    .irqs = xhci_irqs,
    .irq_count = countof(xhci_irqs),
    .btis = xhci_btis,
    .bti_count = countof(xhci_btis),
};

zx_status_t gauss_usb_init(gauss_bus_t* bus) {
    zx_status_t status = io_buffer_init_physical(&bus->usb_phy, bus->bti_handle, 0xffe09000, 4096,
                                                 get_root_resource(),
                                                 ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "gauss_usb_init io_buffer_init_physical failed %d\n", status);
        return status;
    }

    status = zx_interrupt_create(get_root_resource(), USB_PHY_IRQ,
                    ZX_INTERRUPT_MODE_DEFAULT, &bus->usb_phy_irq_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "gauss_usb_init zx_interrupt_create failed %d\n", status);
        io_buffer_release(&bus->usb_phy);
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

    if ((status = pbus_device_add(&bus->pbus, &xhci_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "a113_usb_init could not add xhci_dev: %d\n", status);
        return status;
    }

    return ZX_OK;
}
