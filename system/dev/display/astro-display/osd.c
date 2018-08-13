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
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>

#include <zircon/syscalls.h>
#include <zircon/assert.h>
#include <hw/reg.h>
#include "astro-display.h"
#include "vpu.h"

void osd_debug_dump_register_all(astro_display_t* display)
{
    uint32_t reg = 0;
    uint32_t offset = 0;
    uint32_t index = 0;

    reg = VPU_VIU_VENC_MUX_CTRL;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VPP_MISC;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VPP_OFIFO_SIZE;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VPP_HOLD_LINES;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));

    reg = VPU_OSD_PATH_MISC_CTRL;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VIU_OSD_BLEND_CTRL;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VIU_OSD_BLEND_DIN0_SCOPE_H;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VIU_OSD_BLEND_DIN0_SCOPE_V;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VIU_OSD_BLEND_DIN1_SCOPE_H;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VIU_OSD_BLEND_DIN1_SCOPE_V;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VIU_OSD_BLEND_DIN2_SCOPE_H;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VIU_OSD_BLEND_DIN2_SCOPE_V;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VIU_OSD_BLEND_DIN3_SCOPE_H;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VIU_OSD_BLEND_DIN3_SCOPE_V;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VIU_OSD_BLEND_DUMMY_DATA0;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VIU_OSD_BLEND_DUMMY_ALPHA;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VIU_OSD_BLEND_BLEND0_SIZE;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VIU_OSD_BLEND_BLEND1_SIZE;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));

    reg = VPU_VPP_OSD1_IN_SIZE;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VPP_OSD1_BLD_H_SCOPE;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VPP_OSD1_BLD_V_SCOPE;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VPP_OSD2_BLD_H_SCOPE;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VPP_OSD2_BLD_V_SCOPE;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_OSD1_BLEND_SRC_CTRL;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_OSD2_BLEND_SRC_CTRL;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VPP_POSTBLEND_H_SIZE;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VPP_OUT_H_V_SIZE;
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
        if (index == 1) {
            offset = 0x20;
        }
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
        if (index == 1) {
            reg = VPU_VIU_OSD2_BLK0_CFG_W4;
        }
        DISP_INFO("reg[0x%x]: 0x%08x\n\n", (reg >> 2), READ32_VPU_REG(reg));
    }
}



enum {
    VPU_VIU_OSD1_BLK_CFG_TBL_ADDR_SHIFT = 16,
    VPU_VIU_OSD1_BLK_CFG_LITTLE_ENDIAN = (1 << 15),
    VPU_VIU_OSD1_BLK_CFG_OSD_BLK_MODE_32_BIT = 5,
    VPU_VIU_OSD1_BLK_CFG_OSD_BLK_MODE_16_BIT = 4,
    VPU_VIU_OSD1_BLK_CFG_OSD_BLK_MODE_SHIFT = 8,
    VPU_VIU_OSD1_BLK_CFG_COLOR_MATRIX_565 = 4,
    VPU_VIU_OSD1_BLK_CFG_COLOR_MATRIX_ARGB = 1,
    VPU_VIU_OSD1_BLK_CFG_COLOR_MATRIX_SHIFT = 2,

    VPU_VIU_OSD1_CTRL_STAT2_REPLACED_ALPHA_EN = (1 << 14),
    VPU_VIU_OSD1_CTRL_STAT2_REPLACED_ALPHA_SHIFT = 6u,
};

void disable_osd(astro_display_t* display) {
    display->current_image_valid = false;
    SET_BIT32(VPU, VPU_VIU_OSD1_CTRL_STAT, 0, 1, 0);
}

zx_status_t configure_osd(astro_display_t* display)
{
    disable_osd(display);
    // TODO: OSD for g12a is slightly different from gxl. Currently, uBoot enables
    // scaling and 16bit mode (565) and configures various layers based on that assumption.
    // Since we don't have a full end-to-end driver at this moment, we cannot simply turn off
    // scaling.
    // For now, we will only configure the OSD layer to use the new Canvas index,
    // and use 32-bit color.
    uint32_t ctrl_stat2 = READ32_VPU_REG(VPU_VIU_OSD1_CTRL_STAT2);
    ctrl_stat2 |= VPU_VIU_OSD1_CTRL_STAT2_REPLACED_ALPHA_EN |
                  (0xff << VPU_VIU_OSD1_CTRL_STAT2_REPLACED_ALPHA_SHIFT);
    // Set to use BGRX instead of BGRA.
    WRITE32_VPU_REG(VPU_VIU_OSD1_CTRL_STAT2, ctrl_stat2);

    return ZX_OK;
}

void flip_osd(astro_display_t* display, uint8_t idx) {
    display->current_image_valid = true;
    display->current_image = idx;
    uint32_t cfg_w0 = (idx << VPU_VIU_OSD1_BLK_CFG_TBL_ADDR_SHIFT) |
        VPU_VIU_OSD1_BLK_CFG_LITTLE_ENDIAN |
        (VPU_VIU_OSD1_BLK_CFG_OSD_BLK_MODE_32_BIT << VPU_VIU_OSD1_BLK_CFG_OSD_BLK_MODE_SHIFT) |
        (VPU_VIU_OSD1_BLK_CFG_COLOR_MATRIX_ARGB << VPU_VIU_OSD1_BLK_CFG_COLOR_MATRIX_SHIFT);

    WRITE32_VPU_REG(VPU_VIU_OSD1_BLK0_CFG_W0, cfg_w0);
    SET_BIT32(VPU, VPU_VIU_OSD1_CTRL_STAT, 1, 1, 0); // Enable OSD
}
