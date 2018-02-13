// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/debug.h>
#include <ddk/binding.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/display.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>

#include <zircon/syscalls.h>
#include <zircon/assert.h>
#include <hw/reg.h>
#include "vim-display.h"
#include "hdmitx.h"

#define WAIT_FOR_PLL_LOCKED(reg)                \
    do {                            \
        unsigned int st = 0, cnt = 1000;          \
        while (cnt--) {                                 \
            usleep(5);              \
            st = !!(READ32_HHI_REG(reg) & (1 << 31));  \
            if (st)                 \
                break;              \
            else { /* reset hpll */         \
                SET_BIT32(HHI, reg, 1, 1, 28); \
                SET_BIT32(HHI, reg, 0, 1, 28); \
            }                   \
        }                       \
        DISP_ERROR("pll[0x%x] reset %d times\n", reg, 999 - cnt);\
    } while (0)

void configure_hpll_clk_out(vim2_display_t* display, uint32_t hpll)
{
    switch (hpll) {
    case 5940000:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x4000027b);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb300);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0xc60f30e0);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 5680000:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x400002ec);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb2ab);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 5405400:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x400002e1);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb0e6);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 5371100:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x400002df);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb32f);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 5200000:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x400002d8);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb2ab);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 4870000:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x400002ca);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb3ab);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 4455000:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x400002b9);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb280);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 4115866:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x400002ab);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb1fa);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 3712500:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x4000029a);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb2c0);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 3450000:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x4000028f);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb300);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 3243240:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x40000287);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb08a);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 3240000:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x40000287);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 2970000:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x4000027b);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb300);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 4324320:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x400002b4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb0b8);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 4320000:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x400002b4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 3180000:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x40000284);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb200);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 3200000:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x40000285);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb155);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 3340000:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x4000028b);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb0ab);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 3420000:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x4000028e);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb200);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 3485000:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x40000291);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb0d5);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 3865000:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x400002a1);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb02b);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 4028000:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x400002a7);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb355);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 4032000:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x400002a8);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 4260000:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x400002b1);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb200);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 4761600:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x400002c6);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb19a);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 4838400:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x400002c9);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb266);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    case 5850000:
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0x400002f3);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, 0x800cb300);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x860f30c4);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, 0x0c8e0000);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, 0x001fa729);
        WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x01a31500);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x1, 1, 28);
        SET_BIT32(HHI, HHI_HDMI_PLL_CNTL, 0x0, 1, 28);
        WAIT_FOR_PLL_LOCKED(HHI_HDMI_PLL_CNTL);
        DISP_INFO("HPLL: 0x%x\n", READ32_HHI_REG(HHI_HDMI_PLL_CNTL));
        break;
    default:
        DISP_ERROR("error hpll clk: %d\n", hpll);
        break;
    }

}

void configure_od3_div(vim2_display_t* display, uint32_t div_sel)
{
    int shift_val = 0;
    int shift_sel = 0;

    /* When div 6.25, need to reset vid_pll_div */
    if (div_sel == VID_PLL_DIV_6p25) {
        usleep(1);
        SET_BIT32(PRESET, PRESET0_REGISTER, 1, 1, 7);
    }
    // Disable the output clock
    SET_BIT32(HHI, HHI_VID_PLL_CLK_DIV, 0, 1, 19);
    SET_BIT32(HHI, HHI_VID_PLL_CLK_DIV, 0, 1, 15);

    switch (div_sel) {
    case VID_PLL_DIV_1:      shift_val = 0xFFFF; shift_sel = 0; break;
    case VID_PLL_DIV_2:      shift_val = 0x0aaa; shift_sel = 0; break;
    case VID_PLL_DIV_3:      shift_val = 0x0db6; shift_sel = 0; break;
    case VID_PLL_DIV_3p5:    shift_val = 0x36cc; shift_sel = 1; break;
    case VID_PLL_DIV_3p75:   shift_val = 0x6666; shift_sel = 2; break;
    case VID_PLL_DIV_4:      shift_val = 0x0ccc; shift_sel = 0; break;
    case VID_PLL_DIV_5:      shift_val = 0x739c; shift_sel = 2; break;
    case VID_PLL_DIV_6:      shift_val = 0x0e38; shift_sel = 0; break;
    case VID_PLL_DIV_6p25:   shift_val = 0x0000; shift_sel = 3; break;
    case VID_PLL_DIV_7:      shift_val = 0x3c78; shift_sel = 1; break;
    case VID_PLL_DIV_7p5:    shift_val = 0x78f0; shift_sel = 2; break;
    case VID_PLL_DIV_12:     shift_val = 0x0fc0; shift_sel = 0; break;
    case VID_PLL_DIV_14:     shift_val = 0x3f80; shift_sel = 1; break;
    case VID_PLL_DIV_15:     shift_val = 0x7f80; shift_sel = 2; break;
    case VID_PLL_DIV_2p5:    shift_val = 0x5294; shift_sel = 2; break;
    default:
        DISP_ERROR("Error: clocks_set_vid_clk_div:  Invalid parameter\n");
        break;
    }

    if (shift_val == 0xffff ) {      // if divide by 1
        SET_BIT32(HHI, HHI_VID_PLL_CLK_DIV, 1, 1, 18);
    } else {
        SET_BIT32(HHI, HHI_VID_PLL_CLK_DIV, 0, 1, 18);
        SET_BIT32(HHI, HHI_VID_PLL_CLK_DIV, 0, 2, 16);
        SET_BIT32(HHI, HHI_VID_PLL_CLK_DIV, 0, 1, 15);
        SET_BIT32(HHI, HHI_VID_PLL_CLK_DIV, 0, 14,  0);

        SET_BIT32(HHI, HHI_VID_PLL_CLK_DIV, shift_sel, 2, 16);
        SET_BIT32(HHI, HHI_VID_PLL_CLK_DIV, 1, 1, 15);
        SET_BIT32(HHI, HHI_VID_PLL_CLK_DIV, shift_val, 14,  0);
        SET_BIT32(HHI, HHI_VID_PLL_CLK_DIV, 0, 1, 15);
    }
    // Enable the final output clock
    SET_BIT32(HHI, HHI_VID_PLL_CLK_DIV, 1, 1, 19);

}

zx_status_t configure_pll(vim2_display_t* display, const struct hdmi_param* p, const struct pll_param* pll)
{
    // Set VIU Mux Ctrl
    SET_BIT32(VPU, VPU_VPU_VIU_VENC_MUX_CTRL, pll->viu_type, 2, (pll->viu_channel == 1) ? 0 : 2);
    SET_BIT32(HHI, HHI_HDMI_CLK_CNTL, 0, 3, 9);
    SET_BIT32(HHI, HHI_HDMI_CLK_CNTL, 0, 7, 0);
    SET_BIT32(HHI, HHI_HDMI_CLK_CNTL, 1, 1, 8);
    configure_hpll_clk_out(display, pll->hpll_clk_out);

    //set od1
    SET_BIT32(HHI, HHI_HDMI_PLL_CNTL2, (pll->od1 >> 1), 2, 21);

    //set od2
    SET_BIT32(HHI, HHI_HDMI_PLL_CNTL2, (pll->od2 >> 1), 2, 23);

    //set od1
    SET_BIT32(HHI, HHI_HDMI_PLL_CNTL2, (pll->od3 >> 1), 2, 19);

    configure_od3_div(display, pll->vid_pll_div);

    SET_BIT32(HHI, HHI_VID_CLK_CNTL, 0, 3, 16);   // select vid_pll_clk
    SET_BIT32(HHI, HHI_VID_CLK_DIV, (pll->vid_clk_div == 0) ? 0 : (pll->vid_clk_div - 1), 8, 0);
    SET_BIT32(HHI, HHI_VID_CLK_CNTL, 7, 3, 0);

    SET_BIT32(HHI, HHI_HDMI_CLK_CNTL, (pll->hdmi_tx_pixel_div == 12) ?
        4 : (pll->hdmi_tx_pixel_div >> 1), 4, 16);
    SET_BIT32(HHI, HHI_VID_CLK_CNTL2, 1, 1, 5);   //enable gate


    if (pll->encp_div != (uint32_t)-1) {
        SET_BIT32(HHI, HHI_VID_CLK_DIV, (pll->encp_div == 12) ?
            4 : (pll->encp_div >> 1), 4, 24);
        SET_BIT32(HHI, HHI_VID_CLK_CNTL2, 1, 1, 2);   //enable gate
        SET_BIT32(HHI, HHI_VID_CLK_CNTL, 1, 1, 19);
    }
    if (pll->enci_div != (uint32_t)-1) {
        SET_BIT32(HHI, HHI_VID_CLK_DIV, (pll->encp_div == 12) ?
            4 : (pll->encp_div >> 1), 4, 28);
        SET_BIT32(HHI, HHI_VID_CLK_CNTL2, 1, 1, 0);   //enable gate
        SET_BIT32(HHI, HHI_VID_CLK_CNTL, 1, 1, 19);
    }

    DISP_INFO("done!\n");
    return ZX_OK;
}

