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

void osd_debug_dump_register_all(vim2_display_t* display)
{
    uint32_t reg = 0;
    uint32_t offset = 0;
    uint32_t index = 0;

    reg = VPU_VPU_VIU_VENC_MUX_CTRL;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VPP_MISC;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VPP_OFIFO_SIZE;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VPP_HOLD_LINES;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VPP_OSD_SC_CTRL0;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VPP_OSD_SCI_WH_M1;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VPP_OSD_SCO_H_START_END;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VPP_OSD_SCO_V_START_END;
    DISP_INFO("reg[0x%x]: 0x%08x\n\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VPP_POSTBLEND_H_SIZE;
    DISP_INFO("reg[0x%x]: 0x%08x\n\n", (reg >> 2), READ32_VPU_REG(reg));

    for (index = 0; index < 2; index++) {
        if (index == 1)
            offset = (0x20 << 2);
        reg = offset + VPU_VIU_OSD1_FIFO_CTRL_STAT;
        DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
        reg = offset + VPU_VIU_OSD1_CTRL_STAT;
        DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
        reg = offset + VPU_VIU_OSD1_BLK0_CFG_W0;
        DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
        reg = offset + VPU_VIU_OSD1_BLK0_CFG_W1;
        DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
        reg = offset + VPU_VIU_OSD1_BLK0_CFG_W2;
        DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
        reg = offset + VPU_VIU_OSD1_BLK0_CFG_W3;
        DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
        reg = VPU_VIU_OSD1_BLK0_CFG_W4;
        if (index == 1)
            reg = VPU_VIU_OSD2_BLK0_CFG_W4;
        DISP_INFO("reg[0x%x]: 0x%08x\n\n", (reg >> 2), READ32_VPU_REG(reg));
    }
}

enum {
    VPU_VIU_OSD2_BLK_CFG_TBL_ADDR_SHIFT = 16,
    VPU_VIU_OSD2_BLK_CFG_LITTLE_ENDIAN = (1 << 15),
    VPU_VIU_OSD2_BLK_CFG_OSD_BLK_MODE_32_BIT = 5,
    VPU_VIU_OSD2_BLK_CFG_OSD_BLK_MODE_SHIFT = 8,
    VPU_VIU_OSD2_BLK_CFG_RGB_EN = (1 << 7),
    VPU_VIU_OSD2_BLK_CFG_COLOR_MATRIX_ARGB = 1,
    VPU_VIU_OSD2_BLK_CFG_COLOR_MATRIX_SHIFT = 2,

    VPU_VIU_OSD2_CTRL_STAT2_REPLACED_ALPHA_EN = (1 << 14),
    VPU_VIU_OSD2_CTRL_STAT2_REPLACED_ALPHA_SHIFT = 6u,
};

zx_status_t configure_osd2(vim2_display_t* display)
{
    uint32_t x_start, x_end, y_start, y_end;
    x_start = y_start = 0;
    x_end = display->disp_info.width - 1;
    y_end = display->disp_info.height - 1;

    // disable scaling
    SET_BIT32(VPU, VPU_VPP_MISC, 0, 1, 12);
    WRITE32_VPU_REG(VPU_VPP_OSD_SC_CTRL0, 0);

    DISP_INFO("0x%x 0x%x\n", READ32_VPU_REG(VPU_VPP_MISC), READ32_VPU_REG(VPU_VPP_OSD_SC_CTRL0));

    uint32_t ctrl_stat2 = READ32_VPU_REG(VPU_VIU_OSD2_CTRL_STAT2);
    WRITE32_VPU_REG(VPU_VIU_OSD2_CTRL_STAT2, ctrl_stat2 | VPU_VIU_OSD2_CTRL_STAT2_REPLACED_ALPHA_EN | (0xff << VPU_VIU_OSD2_CTRL_STAT2_REPLACED_ALPHA_SHIFT));

    uint32_t cfg_w0 = (OSD2_DMC_CAV_INDEX << VPU_VIU_OSD2_BLK_CFG_TBL_ADDR_SHIFT) |
                      VPU_VIU_OSD2_BLK_CFG_LITTLE_ENDIAN | VPU_VIU_OSD2_BLK_CFG_RGB_EN |
                      (VPU_VIU_OSD2_BLK_CFG_OSD_BLK_MODE_32_BIT << VPU_VIU_OSD2_BLK_CFG_OSD_BLK_MODE_SHIFT) |
                      (VPU_VIU_OSD2_BLK_CFG_COLOR_MATRIX_ARGB << VPU_VIU_OSD2_BLK_CFG_COLOR_MATRIX_SHIFT);

    WRITE32_VPU_REG(VPU_VIU_OSD2_BLK0_CFG_W0, cfg_w0);
    WRITE32_VPU_REG(VPU_VIU_OSD2_BLK0_CFG_W1, (x_end << 16) | (x_start));
    WRITE32_VPU_REG(VPU_VIU_OSD2_BLK0_CFG_W3, (x_end << 16) | (x_start));

    WRITE32_VPU_REG(VPU_VIU_OSD2_BLK0_CFG_W2, (y_end << 16) | (y_start));
    WRITE32_VPU_REG(VPU_VIU_OSD2_BLK0_CFG_W4, (y_end << 16) | (y_start));

    WRITE32_VPU_REG(VPU_VPP_OSD_SCO_H_START_END, 0);
    WRITE32_VPU_REG(VPU_VPP_OSD_SCO_V_START_END, 0);

    WRITE32_VPU_REG(VPU_VPP_POSTBLEND_H_SIZE, display->disp_info.width);

    WRITE32_VPU_REG(VPU_VPP_OSD_SCI_WH_M1, 0);

    return ZX_OK;
}
