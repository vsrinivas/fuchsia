// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/io-buffer.h>
#include <hw/reg.h>
#include <soc/aml-common/aml-usb-phy-v2-regs.h>
#include <soc/aml-s905d2/s905d2-hw.h>

// from mesong12a.dtsi
#define PLL_SETTING_0   0x09400414
#define PLL_SETTING_1   0x927E0000
#define PLL_SETTING_2   0xac5f49e5

// set_usb_pll() in phy_aml_new_usb2_v2.c
static zx_status_t set_usb_pll(zx_paddr_t reg_base, zx_handle_t bti) {
    io_buffer_t buf;
    zx_status_t status;

    status = io_buffer_init_physical(&buf, bti, reg_base, PAGE_SIZE, get_root_resource(),
                                     ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        return status;
    }
    void* reg = io_buffer_virt(&buf);

    writel((0x30000000 | PLL_SETTING_0), reg + 0x40);
    writel(PLL_SETTING_1, reg + 0x44);
    writel(PLL_SETTING_2, reg + 0x48);
    zx_nanosleep(zx_deadline_after(ZX_USEC(100)));
    writel((0x10000000 | PLL_SETTING_0), reg + 0x40);

    io_buffer_release(&buf);
    return ZX_OK;
}

zx_status_t aml_usb_phy_v2_init(zx_handle_t bti) {
    zx_status_t status;
    io_buffer_t reset_buf;
    io_buffer_t usbctrl_buf;

    status = io_buffer_init_physical(&reset_buf, bti, S905D2_RESET_BASE, S905D2_RESET_LENGTH,
                                     get_root_resource(),
                                     ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_usb_init io_buffer_init_physical failed %d\n", status);
        return status;
    }

    status = io_buffer_init_physical(&usbctrl_buf, bti, S905D2_USBCTRL_BASE, S905D2_USBCTRL_LENGTH,
                                     get_root_resource(),
                                     ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_usb_init io_buffer_init_physical failed %d\n", status);
        io_buffer_release(&reset_buf);
        return status;
    }

    volatile void* reset_regs = io_buffer_virt(&reset_buf);
    volatile void* usbctrl_regs = io_buffer_virt(&usbctrl_buf);

    // first reset USB
    uint32_t val = readl(reset_regs + 0x21 * 4);
    writel((val | (0x3 << 16)), reset_regs + 0x21 * 4);

    volatile uint32_t* reset_1 = (uint32_t *)(reset_regs + S905D2_RESET1_REGISTER);
    writel(readl(reset_1) | S905D2_RESET1_USB, reset_1);
    // FIXME(voydanoff) this delay is very long, but it is what the Amlogic Linux kernel is doing.
    zx_nanosleep(zx_deadline_after(ZX_MSEC(500)));

    // amlogic_new_usb2_init
    for (int i = 0; i < 2; i++) {
        volatile void* addr = usbctrl_regs + (i * PHY_REGISTER_SIZE) + U2P_R0_OFFSET;
        uint32_t temp = readl(addr);
        temp |= U2P_R0_POR;
        temp |= U2P_R0_HOST_DEVICE;
        if (i == 1) {
            temp |= U2P_R0_IDPULLUP0;
            temp |= U2P_R0_DRVVBUS0;
        }
        writel(temp, addr);

        zx_nanosleep(zx_deadline_after(ZX_USEC(10)));

        writel(readl(reset_1) | (1 << (16 + i)), reset_1);

        zx_nanosleep(zx_deadline_after(ZX_USEC(50)));

        addr = usbctrl_regs + (i * PHY_REGISTER_SIZE) + USB_R1_OFFSET;

        temp = readl(addr);
        int cnt = 0;
        while (!(temp & U2P_R1_PHY_RDY)) {
            temp = readl(addr);
            // wait phy ready max 1ms, common is 100us
            if (cnt > 200) {
                zxlogf(ERROR, "aml_usb_init U2P_R1_PHY_RDY wait failed\n");
                break;
            }

            cnt++;
            zx_nanosleep(zx_deadline_after(ZX_USEC(5)));
        }
    }

    // set up PLLs
    if ((status = set_usb_pll(S905D2_USBPHY20_BASE, bti)) != ZX_OK ||
        (status = set_usb_pll(S905D2_USBPHY21_BASE, bti)) != ZX_OK) {
        zxlogf(ERROR, "aml_usb_init: set_usb_pll failed: %d\n", status);
    }

    io_buffer_release(&reset_buf);
    io_buffer_release(&usbctrl_buf);

    return ZX_OK;
}
