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

static const pbus_mmio_t dwc3_mmios[] = {
    {
        .base = DWC3_MMIO_BASE,
        .length = DWC3_MMIO_LENGTH,
    },
};

static const pbus_irq_t dwc3_irqs[] = {
    {
        .irq = DWC3_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_bti_t usb_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_USB_XHCI,
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
    .btis = usb_btis,
    .bti_count = countof(usb_btis),
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
    .btis = usb_btis,
    .bti_count = countof(usb_btis),
};

// based on code from phy-aml-new-usb3.c
static int phy_irq_thread(void* arg) {
    gauss_bus_t* bus = arg;
    volatile void* addr = io_buffer_virt(&bus->usb_phy);
    volatile void* u2p_regs = addr + PHY_REGISTER_SIZE;
    volatile void* usb_regs = addr + (4 * PHY_REGISTER_SIZE);
    uint32_t temp;

    gpio_config(&bus->gpio, USB_VBUS_GPIO, GPIO_DIR_OUT);

    while (1) {
        uint64_t slots;
        zx_status_t status = zx_interrupt_wait(bus->usb_phy_irq_handle, &slots);
        if (status != ZX_OK) {
            zxlogf(ERROR, "phy_irq_thread: zx_interrupt_wait returned %d\n", status);
            break;
        }
        if (slots & (1ul << ZX_INTERRUPT_SLOT_USER)) {
            break;
        }

        temp = readl(usb_regs + USB_R5_OFFSET);
        temp &= ~USB_R5_IDDIG_IRQ;
        writel(temp, usb_regs + USB_R5_OFFSET);

        zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));

        temp = readl(usb_regs + USB_R5_OFFSET);
        bool host = !(temp & USB_R5_IDDIG_CURR);
        zxlogf(INFO, "phy_irq_thread setting mode %s\n", (host ? "HOST" : "DEVICE"));

        if (host) {
            gpio_write(&bus->gpio, USB_VBUS_GPIO, 1);
        }

        temp = readl(usb_regs + USB_R0_OFFSET);
        if (host) {
            temp &= ~USB_R0_U2D_ACT;
        } else {
            temp |= USB_R0_U2D_ACT;
        }
        writel(temp, usb_regs + USB_R0_OFFSET);

        temp = readl(usb_regs + USB_R4_OFFSET);
        if (host) {
            temp &= ~USB_R4_P21_SLEEPM0;
        } else {
            temp |= USB_R4_P21_SLEEPM0;
        }
        writel(temp, usb_regs + USB_R4_OFFSET);


        temp = readl(u2p_regs + U2P_R0_OFFSET);
        if (host) {
            temp |= U2P_R0_DMPULLDOWN;
            temp |= U2P_R0_DPPULLDOWN;
            temp |= U2P_R0_POR;
        } else {
            temp &= ~U2P_R0_DMPULLDOWN;
            temp &= ~U2P_R0_DPPULLDOWN;
            temp |= U2P_R0_POR;
        }
        writel(temp, u2p_regs + U2P_R0_OFFSET);

        zx_nanosleep(zx_deadline_after(ZX_USEC(500)));

        temp = readl(u2p_regs + U2P_R0_OFFSET);
        temp &= ~U2P_R0_POR;
        writel(temp, u2p_regs + U2P_R0_OFFSET);

        if (!host) {
            gpio_write(&bus->gpio, USB_VBUS_GPIO, 0);
        }
    }
    return 0;
}

zx_status_t gauss_usb_init(gauss_bus_t* bus) {
    zx_status_t status = io_buffer_init_physical(&bus->usb_phy, bus->bti_handle, 0xffe09000, 4096,
                                                 get_root_resource(),
                                                 ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "gauss_usb_init io_buffer_init_physical failed %d\n", status);
        return status;
    }

    status = zx_interrupt_create(get_root_resource(), 0, &bus->usb_phy_irq_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "gauss_usb_init zx_interrupt_create failed %d\n", status);
        io_buffer_release(&bus->usb_phy);
        return status;
    }
    status = zx_interrupt_bind(bus->usb_phy_irq_handle, 0, get_root_resource(), USB_PHY_IRQ,
                               ZX_INTERRUPT_MODE_DEFAULT);
    if (status != ZX_OK) {
        zxlogf(ERROR, "gauss_usb_init zx_interrupt_bind failed %d\n", status);
        zx_handle_close(bus->usb_phy_irq_handle);
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

    // add dwc3 device
    if ((status = pbus_device_add(&bus->pbus, &dwc3_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "a113_usb_init could not add dwc3_dev: %d\n", status);
        return status;
    }
    // xhci_dev is enabled/disabled dynamically, so don't enable it here
    if ((status = pbus_device_add(&bus->pbus, &xhci_dev, PDEV_ADD_DISABLED)) != ZX_OK) {
        zxlogf(ERROR, "a113_usb_init could not add xhci_dev: %d\n", status);
        return status;
    }

    thrd_create_with_name(&bus->phy_irq_thread, phy_irq_thread, bus, "phy_irq_thread");

    return ZX_OK;
}

zx_status_t gauss_usb_set_mode(gauss_bus_t* bus, usb_mode_t mode) {
    // TODO(voydanoff) more work will be needed here for switching to peripheral mode

    // add or remove XHCI device
    pbus_device_enable(&bus->pbus, PDEV_VID_GENERIC, PDEV_PID_GENERIC, PDEV_DID_USB_XHCI,
                       mode == USB_MODE_HOST);
    return ZX_OK;
}
