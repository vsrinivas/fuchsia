// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <soc/imx8m/imx8m.h>
#include <soc/imx8m/imx8m-hw.h>
#include <soc/imx8m/imx8m-iomux.h>
#include <soc/imx8m/imx8m-gpio.h>
#include <soc/imx8m/imx8m-sip.h>
#include <limits.h>
#include <hw/reg.h>
#include <zircon/syscalls/smc.h>
#include "imx8mevk.h"

static const pbus_mmio_t usb1_mmios[] = {
    {
        .base = IMX8M_USB1_BASE,
        .length = IMX8M_USB1_LENGTH,
    },
};
static const pbus_irq_t usb1_irqs[] = {
    {
        .irq = IMX8M_A53_INTR_USB1,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_bti_t usb1_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_USB1,
    },
};

static usb_mode_t usb1_mode = USB_MODE_HOST;

static const pbus_metadata_t usb1_metadata[] = {
    {
        .type       = DEVICE_METADATA_USB_MODE,
        .extra      = 0,
        .data       = &usb1_mode,
        .len        = sizeof(usb1_mode),
    }
};

// USB1 is USB-C OTG port
static const pbus_dev_t usb1_dev = {
    .name = "dwc3-1",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_USB_DWC3,
    .mmios = usb1_mmios,
    .mmio_count = countof(usb1_mmios),
    .irqs = usb1_irqs,
    .irq_count = countof(usb1_irqs),
    .btis = usb1_btis,
    .bti_count = countof(usb1_btis),
    .metadata = usb1_metadata,
    .metadata_count = countof(usb1_metadata),
};

static const pbus_mmio_t usb2_mmios[] = {
    {
        .base = IMX8M_USB2_BASE,
        .length = IMX8M_USB2_LENGTH,
    },
};
static const pbus_irq_t usb2_irqs[] = {
    {
        .irq = IMX8M_A53_INTR_USB2,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_bti_t usb2_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_USB2,
    },
};

static usb_mode_t usb2_mode = USB_MODE_HOST;

static const pbus_metadata_t usb2_metadata[] = {
    {
        .type       = DEVICE_METADATA_USB_MODE,
        .extra      = 0,
        .data       = &usb2_mode,
        .len        = sizeof(usb2_mode),
    }
};

// USB1 is USB-A port, host only
static const pbus_dev_t usb2_dev = {
    .name = "dwc3-2",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_USB_DWC3,
    .mmios = usb2_mmios,
    .mmio_count = countof(usb2_mmios),
    .irqs = usb2_irqs,
    .irq_count = countof(usb2_irqs),
    .btis = usb2_btis,
    .bti_count = countof(usb2_btis),
    .metadata = usb2_metadata,
    .metadata_count = countof(usb2_metadata),
};

zx_status_t imx_usb_phy_init(zx_paddr_t usb_base, size_t usb_length, zx_handle_t bti) {
    uint32_t reg;
    io_buffer_t usb_buf;
    zx_status_t status = io_buffer_init_physical(&usb_buf, bti, usb_base, usb_length,
                                        get_root_resource(), ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s io_buffer_init_physical failed %d\n", __FUNCTION__, status);
        return status;
    }

    volatile void* regs = io_buffer_virt(&usb_buf);
    //TODO: More stuff might be needed if we were to boot from our own bootloader.
    reg = readl(regs + USB_PHY_CTRL1);
    reg &= ~(PHY_CTRL1_VDATSRCENB0 | PHY_CTRL1_VDATDETENB0);
    reg |= PHY_CTRL1_RESET | PHY_CTRL1_ATERESET;
    writel(reg, regs + USB_PHY_CTRL1);

    reg = readl(regs + USB_PHY_CTRL0);
    reg |= PHY_CTRL0_REF_SSP_EN;
    writel(reg, regs + USB_PHY_CTRL0);

    reg = readl(regs + USB_PHY_CTRL2);
    reg |= PHY_CTRL2_TXENABLEN0;
    writel(reg, regs + USB_PHY_CTRL2);

    reg = readl(regs + USB_PHY_CTRL1);
    reg &= ~(PHY_CTRL1_RESET | PHY_CTRL1_ATERESET);
    writel(reg, regs + USB_PHY_CTRL1);

    io_buffer_release(&usb_buf);
    return ZX_OK;
}

zx_status_t imx_usb_init(imx8mevk_bus_t* bus) {
    zx_status_t status;
    zx_handle_t bti;

    // turn on usb via smc calls
    zx_smc_parameters_t otg1_en_params = {.func_id = IMX8M_SIP_GPC,
                                          .arg1 = IMX8M_SIP_CONFIG_GPC_PM_DOMAIN,
                                          .arg2 = IMX8M_PD_USB_OTG1,
                                          .arg3 = 1};
    zx_smc_result_t smc_result;
    status = zx_smc_call(get_root_resource(), &otg1_en_params, &smc_result);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: SMC call to turn USB on failed %d\n", __FUNCTION__, status);
        return status;
    }

    zx_smc_parameters_t otg2_en_params = {.func_id = IMX8M_SIP_GPC,
                                          .arg1 = IMX8M_SIP_CONFIG_GPC_PM_DOMAIN,
                                          .arg2 = IMX8M_PD_USB_OTG2,
                                          .arg3 = 1};
    status = zx_smc_call(get_root_resource(), &otg2_en_params, &smc_result);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: SMC call to turn USB on failed %d\n", __FUNCTION__, status);
        return status;
    }

    status = iommu_get_bti(&bus->iommu, 0, BTI_BOARD, &bti);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: iommu_get_bti failed %d\n", __FUNCTION__, status);
        return status;
    }

    status = imx_usb_phy_init(IMX8M_USB1_BASE, IMX8M_USB1_LENGTH, bti);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: imx_usb_phy_init failed %d\n", __FUNCTION__, status);
        zx_handle_close(bti);
        return status;
    }
    status = imx_usb_phy_init(IMX8M_USB2_BASE, IMX8M_USB2_LENGTH, bti);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: imx_usb_phy_init failed %d\n", __FUNCTION__, status);
        zx_handle_close(bti);
        return status;
    }
    zx_handle_close(bti);

    if ((status = pbus_device_add(&bus->pbus, &usb1_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "imx_usb_init could not add usb1_dev: %d\n", status);
        return status;
    }
    if ((status = pbus_device_add(&bus->pbus, &usb2_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "imx_usb_init could not add usb2_dev: %d\n", status);
        return status;
    }
    return ZX_OK;
}

