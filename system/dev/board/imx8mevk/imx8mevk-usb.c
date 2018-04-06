// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <soc/imx8m/imx8m.h>
#include <soc/imx8m/imx8m-hw.h>
#include <soc/imx8m/imx8m-iomux.h>
#include <soc/imx8m/imx8m-gpio.h>
#include <limits.h>
#include <hw/reg.h>
#include "imx8mevk.h"

#define BIT(nr)         (1UL << (nr))
#define USBMIX_PHY_OFFSET       0xF0040
#define PHY_CTRL0_REF_SSP_EN        BIT(2)
#define PHY_CTRL1_RESET             BIT(0)
#define PHY_CTRL1_ATERESET          BIT(3)
#define PHY_CTRL1_VDATSRCENB0       BIT(19)
#define PHY_CTRL1_VDATDETENB0       BIT(20)
#define PHY_CTRL2_TXENABLEN0        BIT(8)
#define USBMIX_PHY_OFFSET           0xF0040
#define PHY_CTRL0_REF_SSP_EN        BIT(2)
#define PHY_CTRL1_RESET             BIT(0)
#define PHY_CTRL1_ATERESET          BIT(3)
#define PHY_CTRL1_VDATSRCENB0       BIT(19)
#define PHY_CTRL1_VDATDETENB0       BIT(20)
#define PHY_CTRL2_TXENABLEN0        BIT(8)

static const pbus_mmio_t xhci_mmios[] = {
    {
        .base = IMX8M_USB2_BASE,
        .length = IMX8M_USB2_LENGTH,
    },
};
static const pbus_irq_t xhci_irqs[] = {
    {
        .irq = IMX8M_A53_INTR_USB2,
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

zx_status_t imx_usb_init(imx8mevk_bus_t* bus) {
    zx_status_t status;
    uint32_t reg;
    zx_handle_t bti;
    status = iommu_get_bti(&bus->iommu, 0, BTI_BOARD, &bti);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: iommu_get_bti failed %d\n", __FUNCTION__, status);
        return status;
    }
    io_buffer_t usb_phy;
    status = io_buffer_init_physical(&usb_phy, bti, IMX8M_USB1_BASE, IMX8M_USB1_LENGTH,
                                        get_root_resource(), ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s io_buffer_init_physical failed %d\n", __FUNCTION__, status);
        zx_handle_close(bti);
        return status;
    }
    volatile void* regs = io_buffer_virt(&usb_phy);
    //TODO: More stuff might be needed if we were to boot from our own bootloader.
    reg = readl(regs + 0x4);
    reg &= ~(PHY_CTRL1_VDATSRCENB0 | PHY_CTRL1_VDATDETENB0);
    reg |= PHY_CTRL1_RESET | PHY_CTRL1_ATERESET;
    writel(reg, regs + 0x4);
    reg = readl(regs + 0x0);
    reg |= PHY_CTRL0_REF_SSP_EN;
    writel(reg, regs + 0x0);
    reg = readl(regs + 0x8);
    reg |= PHY_CTRL2_TXENABLEN0;
    writel(reg, regs + 0x8);
    reg = readl(regs + 0x4);
    reg &= ~(PHY_CTRL1_RESET | PHY_CTRL1_ATERESET);
    writel(reg, regs + 0x4);
    io_buffer_release(&usb_phy);
    zx_handle_close(bti);
    if ((status = pbus_device_add(&bus->pbus, &xhci_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "%s could not add xhci_dev: %d\n", __FUNCTION__, status);
        return status;
    }
    return ZX_OK;
}

