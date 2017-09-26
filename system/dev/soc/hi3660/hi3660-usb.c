// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/driver.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-devices.h>
#include <hw/reg.h>
#include <stdio.h>

#include "hi3660-bus.h"
#include "hi3660-regs.h"
#include "hikey960-hw.h"

zx_status_t hi3360_usb_init(hi3660_bus_t* bus) {
    volatile void* usb3otg_bc = bus->usb3otg_bc.vaddr;
    volatile void* peri_crg = bus->peri_crg.vaddr;
    volatile void* pctrl = bus->pctrl.vaddr;
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

zx_status_t hi3660_usb_set_mode(hi3660_bus_t* bus, usb_mode_t mode) {
    if (mode == bus->usb_mode) {
        return ZX_OK;
    }

    gpio_protocol_t gpio;
    if (pdev_get_protocol(&bus->pdev, ZX_PROTOCOL_GPIO, &gpio) != ZX_OK) {
        printf("hi3360_usb_init: could not get GPIO protocol!\n");
        return ZX_ERR_INTERNAL;
    }

    gpio_config(&gpio, GPIO_HUB_VDD33_EN, GPIO_DIR_OUT);
    gpio_config(&gpio, GPIO_VBUS_TYPEC, GPIO_DIR_OUT);
    gpio_config(&gpio, GPIO_USBSW_SW_SEL, GPIO_DIR_OUT);

    gpio_write(&gpio, GPIO_HUB_VDD33_EN, mode == USB_MODE_HOST);
    gpio_write(&gpio, GPIO_VBUS_TYPEC, mode == USB_MODE_HOST);
    gpio_write(&gpio, GPIO_USBSW_SW_SEL, mode == USB_MODE_HOST);

    // add or remove XHCI device
    pbus_device_enable(&bus->pbus, PDEV_VID_GENERIC, PDEV_PID_GENERIC, PDEV_DID_USB_XHCI,
                       mode == USB_MODE_HOST);

    bus->usb_mode = mode;
    return ZX_OK;
}
