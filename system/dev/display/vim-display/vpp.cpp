// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hdmitx.h"
#include "registers.h"
#include "vim-display.h"
#include <hw/reg.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

void osd_debug_dump_register_all(vim2_display_t* display) {
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

void disable_vd(vim2_display* display, uint32_t vd_index) {
    display->vd1_image_valid = false;
    auto* const vpu = &*display->mmio_vpu;
    registers::Vd(vd_index).IfGenReg().ReadFrom(vpu).set_enable(false).WriteTo(vpu);
    registers::VpuVppMisc::Get()
        .ReadFrom(vpu)
        .set_vd1_enable_postblend(false)
        .WriteTo(vpu);
}

void configure_vd(vim2_display* display, uint32_t vd_index) {
    disable_vd(display, vd_index);
    auto* const vpu = &*display->mmio_vpu;
    uint32_t x_start, x_end, y_start, y_end;
    x_start = y_start = 0;
    x_end = display->cur_display_mode.h_addressable - 1;
    y_end = display->cur_display_mode.v_addressable - 1;

    auto vd = registers::Vd(vd_index);
    vd.IfLumaX0().FromValue(0).set_end(x_end).set_start(x_start).WriteTo(vpu);
    vd.IfLumaY0().FromValue(0).set_end(y_end).set_start(y_start).WriteTo(vpu);
    vd.IfChromaX0().FromValue(0).set_end(x_end / 2).set_start(x_start / 2).WriteTo(vpu);
    vd.IfChromaY0().FromValue(0).set_end(y_end / 2).set_start(y_start / 2).WriteTo(vpu);
    vd.IfGenReg2().FromValue(0).set_color_map(1).WriteTo(vpu);
    vd.FmtCtrl()
        .FromValue(0)
        .set_vertical_enable(true)
        .set_vertical_phase_step(8)
        .set_vertical_initial_phase(0xc)
        .set_vertical_repeat_line0(true)
        .set_horizontal_enable(true)
        .set_horizontal_yc_ratio(1)
        .WriteTo(vpu);
    vd.FmtW()
        .FromValue(0)
        .set_horizontal_width(display->cur_display_mode.h_addressable)
        .set_vertical_width(display->cur_display_mode.h_addressable / 2)
        .WriteTo(vpu);

    vd.IfRptLoop().FromValue(0).WriteTo(vpu);
    vd.IfLuma0RptPat().FromValue(0).WriteTo(vpu);
    vd.IfChroma0RptPat().FromValue(0).WriteTo(vpu);
    vd.IfLumaPsel().FromValue(0).WriteTo(vpu);
    vd.IfChromaPsel().FromValue(0).WriteTo(vpu);
}

void flip_vd(vim2_display* display, uint32_t vd_index, uint32_t index) {
    display->vd1_image_valid = true;
    display->vd1_image = index;
    auto* const vpu = &*display->mmio_vpu;
    auto vd = registers::Vd(vd_index);
    vd.IfGenReg()
        .FromValue(0)
        .set_enable(true)
        .set_separate_en(true)
        .set_chro_rpt_lastl_ctrl(true)
        .set_hold_lines(3)
        .set_urgent_luma(true)
        .set_urgent_chroma(true)
        .WriteTo(vpu);
    vd.IfCanvas0().FromValue(index).WriteTo(vpu);
    registers::VpuVppMisc::Get()
        .ReadFrom(vpu)
        .set_vd1_enable_postblend(true)
        .WriteTo(vpu);
}

void disable_osd(vim2_display_t* display, uint32_t osd_index) {
    display->current_image_valid = false;
    auto* const vpu = &*display->mmio_vpu;
    auto osd = registers::Osd(osd_index);
    osd
        .CtrlStat()
        .ReadFrom(vpu)
        .set_osd_blk_enable(false)
        .WriteTo(vpu);
    if (osd_index == 0) {
        registers::VpuVppMisc::Get()
            .ReadFrom(vpu)
            .set_osd1_enable_postblend(false)
            .WriteTo(vpu);
    } else {
        registers::VpuVppMisc::Get()
            .ReadFrom(vpu)
            .set_osd2_enable_postblend(false)
            .WriteTo(vpu);
    }
}

// Disables the OSD until a flip happens
zx_status_t configure_osd(vim2_display_t* display, uint32_t osd_index) {
    uint32_t x_start, x_end, y_start, y_end;
    x_start = y_start = 0;
    x_end = display->cur_display_mode.h_addressable - 1;
    y_end = display->cur_display_mode.v_addressable - 1;

    disable_osd(display, osd_index);
    auto* const vpu = &*display->mmio_vpu;
    auto osd = registers::Osd(osd_index);
    registers::VpuVppOsdScCtrl0::Get().FromValue(0).WriteTo(vpu);

    osd.CtrlStat2()
        .ReadFrom(vpu)
        .set_replaced_alpha_en(true)
        .set_replaced_alpha(0xff)
        .WriteTo(vpu);

    osd.Blk0CfgW1()
        .FromValue(0)
        .set_virtual_canvas_x_end(x_end)
        .set_virtual_canvas_x_start(x_start)
        .WriteTo(vpu);
    osd.Blk0CfgW2()
        .FromValue(0)
        .set_virtual_canvas_y_end(y_end)
        .set_virtual_canvas_y_start(y_start)
        .WriteTo(vpu);
    osd.Blk0CfgW3().FromValue(0).set_display_h_end(x_end).set_display_h_start(x_start).WriteTo(
        vpu);
    osd.Blk0CfgW4().FromValue(0).set_display_v_end(y_end).set_display_v_start(y_start).WriteTo(
        vpu);

    registers::VpuVppOsdScoHStartEnd::Get().FromValue(0).WriteTo(vpu);
    registers::VpuVppOsdScoVStartEnd::Get().FromValue(0).WriteTo(vpu);

    registers::VpuVppPostblendHSize::Get()
        .FromValue(display->cur_display_mode.h_addressable)
        .WriteTo(vpu);
    registers::VpuVppOsdSciWhM1::Get().FromValue(0).WriteTo(vpu);

    return ZX_OK;
}

void flip_osd(vim2_display_t* display, uint32_t osd_index, uint8_t idx) {
    display->current_image = idx;
    display->current_image_valid = true;
    auto* const vpu = &*display->mmio_vpu;
    auto osd = registers::Osd(osd_index);
    osd.Blk0CfgW0()
        .FromValue(0)
        .set_tbl_addr(idx)
        .set_little_endian(true)
        .set_block_mode(registers::VpuViuOsdBlk0CfgW0::kBlockMode32Bit)
        .set_rgb_en(true)
        .set_color_matrix(registers::VpuViuOsdBlk0CfgW0::kColorMatrixARGB8888)
        .WriteTo(vpu);
    osd.CtrlStat()
        .ReadFrom(vpu)
        .set_osd_blk_enable(true)
        .WriteTo(vpu);
    if (osd_index == 0) {
        registers::VpuVppMisc::Get()
            .ReadFrom(vpu)
            .set_osd1_enable_postblend(true)
            .WriteTo(vpu);
    } else {
        registers::VpuVppMisc::Get()
            .ReadFrom(vpu)
            .set_osd2_enable_postblend(true)
            .WriteTo(vpu);
    }
}
