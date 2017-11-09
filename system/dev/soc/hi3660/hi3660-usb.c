// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/driver.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-defs.h>
#include <hw/reg.h>
#include <soc/hi3660/hi3660.h>
#include <soc/hi3660/hi3660-regs.h>

#include <stdio.h>

zx_status_t hi3660_usb_init(hi3660_t* hi3660) {
    volatile void* usb3otg_bc = io_buffer_virt(&hi3660->usb3otg_bc);
    volatile void* peri_crg = io_buffer_virt(&hi3660->peri_crg);
    volatile void* pctrl = io_buffer_virt(&hi3660->pctrl);
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
