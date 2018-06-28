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
#include <soc/imx8m/imx8m-sip.h>
#include <limits.h>
#include <hw/reg.h>
#include <zircon/syscalls/smc.h>
#include "imx8mevk.h"

static const pbus_mmio_t dwc3_mmios[] = {
    {
        .base = IMX8M_USB2_BASE,
        .length = IMX8M_USB2_LENGTH,
    },
};
static const pbus_irq_t dwc3_irqs[] = {
    {
        .irq = IMX8M_A53_INTR_USB2,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};
static const pbus_bti_t dwc3_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_USB,
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
    .btis = dwc3_btis,
    .bti_count = countof(dwc3_btis),
};

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

    uint32_t reg;
    io_buffer_t usb_phy;
    status = io_buffer_init_physical(&usb_phy, bti, IMX8M_USB2_BASE, IMX8M_USB2_LENGTH,
                                        get_root_resource(), ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s io_buffer_init_physical failed %d\n", __FUNCTION__, status);
        zx_handle_close(bti);
        return status;
    }

    volatile void* regs = io_buffer_virt(&usb_phy);
    regs += USBMIX_PHY_OFFSET;
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

    if ((status = pbus_device_add(&bus->pbus, &dwc3_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "hi3360_add_devices could not add dwc3_dev: %d\n", status);
        return status;
    }
    return ZX_OK;
}

