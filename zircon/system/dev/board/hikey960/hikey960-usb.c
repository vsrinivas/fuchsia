// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/usb/modeswitch.h>
#include <hw/reg.h>
#include <soc/hi3660/hi3660-hw.h>
#include <soc/hi3660/hi3660-regs.h>

#include <stdio.h>

#include "hikey960.h"
#include "hikey960-hw.h"

zx_status_t hi3660_usb_init(hi3660_t* hi3660) {
    volatile void* usb3otg_bc = hi3660->usb3otg_bc.vaddr;
    volatile void* peri_crg = hi3660->peri_crg.vaddr;
    volatile void* pctrl = hi3660->pctrl.vaddr;
    uint32_t temp;

    writel(PERI_CRG_ISODIS_REFCLK_ISO_EN, peri_crg + PERI_CRG_ISODIS);
    writel(PCTRL_CTRL3_USB_TCXO_EN | (PCTRL_CTRL3_USB_TCXO_EN << PCTRL_CTRL3_MSK_START),
           pctrl + PCTRL_CTRL3);

    temp = readl(pctrl + PCTRL_CTRL24);
    temp &= ~PCTRL_CTRL24_SC_CLK_USB3PHY_3MUX1_SEL;
    writel(temp, pctrl + PCTRL_CTRL24);

    writel(PERI_CRG_GT_CLK_USB3OTG_REF | PERI_CRG_GT_ACLK_USB3OTG, peri_crg + PERI_CRG_CLK_EN4);
    writel(PERI_CRG_IP_RST_USB3OTG_MUX | PERI_CRG_IP_RST_USB3OTG_AHBIF
           | PERI_CRG_IP_RST_USB3OTG_32K,  peri_crg + PERI_CRG_RSTDIS4);

    writel(PERI_CRG_IP_RST_USB3OTGPHY_POR | PERI_CRG_IP_RST_USB3OTG, peri_crg + PERI_CRG_RSTEN4);

    // enable PHY REF CLK
    temp = readl(usb3otg_bc + USB3OTG_CTRL0);
    temp |= USB3OTG_CTRL0_ABB_GT_EN;
    writel(temp, usb3otg_bc + USB3OTG_CTRL0);

    temp = readl(usb3otg_bc + USB3OTG_CTRL7);
    temp |= USB3OTG_CTRL7_REF_SSP_EN;
    writel(temp, usb3otg_bc + USB3OTG_CTRL7);

    // exit from IDDQ mode
    temp = readl(usb3otg_bc + USB3OTG_CTRL2);
    temp &= ~(USB3OTG_CTRL2_POWERDOWN_HSP | USB3OTG_CTRL2_POWERDOWN_SSP);
    writel(temp, usb3otg_bc + USB3OTG_CTRL2);
    zx_nanosleep(zx_deadline_after(ZX_USEC(100)));

    writel(PERI_CRG_IP_RST_USB3OTGPHY_POR, peri_crg + PERI_CRG_RSTDIS4);
    writel(PERI_CRG_IP_RST_USB3OTG, peri_crg + PERI_CRG_RSTDIS4);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(20)));

    temp = readl(usb3otg_bc + USB3OTG_CTRL3);
    temp |= (USB3OTG_CTRL3_VBUSVLDEXT | USB3OTG_CTRL3_VBUSVLDEXTSEL);
    writel(temp, usb3otg_bc + USB3OTG_CTRL3);
    zx_nanosleep(zx_deadline_after(ZX_USEC(100)));

    return ZX_OK;
}

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
