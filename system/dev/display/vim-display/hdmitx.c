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
#include "vim-display.h"
#include "hdmitx.h"

// Uncomment to print all HDMI REG writes
// #define LOG_HDMITX


void hdmitx_writereg(const vim2_display_t* display, uint32_t addr, uint32_t data) {
    // determine if we are writing to HDMI TOP (AMLOGIC Wrapper) or HDMI IP
    uint32_t offset = (addr & DWC_OFFSET_MASK) >> 24;
    addr = addr & 0xffff;
    WRITE32_HDMITX_REG(HDMITX_ADDR_PORT + offset, addr);
    WRITE32_HDMITX_REG(HDMITX_ADDR_PORT + offset, addr); // FIXME: Need to write twice!
    WRITE32_HDMITX_REG(HDMITX_DATA_PORT + offset, data);
#ifdef LOG_HDMITX
    DISP_INFO("%s wr[0x%x] 0x%x\n", offset ? "DWC" : "TOP",
            addr, data);
#endif
}

uint32_t hdmitx_readreg(const vim2_display_t* display, uint32_t addr) {
    // determine if we are writing to HDMI TOP (AMLOGIC Wrapper) or HDMI IP
    uint32_t offset = (addr & DWC_OFFSET_MASK) >> 24;
    addr = addr & 0xffff;
    WRITE32_HDMITX_REG(HDMITX_ADDR_PORT + offset, addr);
    WRITE32_HDMITX_REG(HDMITX_ADDR_PORT + offset, addr); // FIXME: Need to write twice!
    return (READ32_HDMITX_REG(HDMITX_DATA_PORT + offset));
}

void hdmi_scdc_read(vim2_display_t* display, uint8_t addr, uint8_t* val) {
    hdmitx_writereg(display, HDMITX_DWC_I2CM_SLAVE, 0x54);
    hdmitx_writereg(display, HDMITX_DWC_I2CM_ADDRESS, addr);
    hdmitx_writereg(display, HDMITX_DWC_I2CM_OPERATION, 1);
    usleep(2000);
    *val = (uint8_t) hdmitx_readreg(display, HDMITX_DWC_I2CM_DATAI);
}

void hdmi_scdc_write(vim2_display_t* display, uint8_t addr, uint8_t val) {
    hdmitx_writereg(display, HDMITX_DWC_I2CM_SLAVE, 0x54);
    hdmitx_writereg(display, HDMITX_DWC_I2CM_ADDRESS, addr);
    hdmitx_writereg(display, HDMITX_DWC_I2CM_DATAO, val);
    hdmitx_writereg(display, HDMITX_DWC_I2CM_OPERATION, 0x10);
    usleep(2000);
}

void hdmi_shutdown(vim2_display_t* display)
{
        /* Close HDMITX PHY */
        WRITE32_REG(HHI,HHI_HDMI_PHY_CNTL0, 0);
        WRITE32_REG(HHI,HHI_HDMI_PHY_CNTL3, 0);
        /* Disable HPLL */
        WRITE32_REG(HHI,HHI_HDMI_PLL_CNTL, 0);
}

zx_status_t init_hdmi_hardware(vim2_display_t* display) {


    /* Step 1: Initialize various clocks related to the HDMI Interface*/
    SET_BIT32(CBUS, PAD_PULL_UP_EN_REG1, 0, 2, 21);
    SET_BIT32(CBUS, PAD_PULL_UP_REG1, 0, 2, 21);
    SET_BIT32(CBUS, P_PREG_PAD_GPIO1_EN_N, 3, 2, 21);
    SET_BIT32(CBUS, PERIPHS_PIN_MUX_6, 3, 2, 29);

    // enable clocks
    SET_BIT32(HHI, HHI_HDMI_CLK_CNTL, 0x0100, 16, 0);

    // enable clk81 (needed for HDMI module and a bunch of other modules)
    SET_BIT32(HHI, HHI_GCLK_MPEG2, 1, 1, 4);

    // power up HDMI Memory (bits 15:8)
    SET_BIT32(HHI, HHI_MEM_PD_REG0, 0, 8, 8);

    // reset hdmi related blocks (HIU, HDMI SYS, HDMI_TX)
    WRITE32_PRESET_REG(PRESET0_REGISTER, (1 << 19));

    /* FIXME: This will reset the entire HDMI subsystem including the HDCP engine.
     * At this point, we have no way of initializing HDCP block, so we need to
     * skip this for now.
    */
    // WRITE32_PRESET_REG(PRESET2_REGISTER, (1 << 15)); // Will mess up hdcp stuff

    WRITE32_PRESET_REG(PRESET2_REGISTER, (1 << 2));

    // Enable APB3 fail on error (TODO: where is this defined?)
    SET_BIT32(HDMITX, 0x8, 1, 1, 15);
    SET_BIT32(HDMITX, 0x18, 1, 1, 15);

    // Bring HDMI out of reset
    hdmitx_writereg(display, HDMITX_TOP_SW_RESET, 0);
    usleep(200);
    hdmitx_writereg(display, HDMITX_TOP_CLK_CNTL, 0x000000ff);
    hdmitx_writereg(display, HDMITX_DWC_MC_LOCKONCLOCK, 0xff);
    hdmitx_writereg(display, HDMITX_DWC_MC_CLKDIS, 0x00);

    /* Step 2: Initialize DDC Interface (For EDID) */

    // FIXME: Pinmux i2c pins (skip for now since uboot it doing it)

    // Configure i2c interface
    // a. disable all interrupts (read_req, done, nack, arbitration)
    hdmitx_writereg(display, HDMITX_DWC_I2CM_INT, 0);
    hdmitx_writereg(display, HDMITX_DWC_I2CM_CTLINT, 0);

    // b. set interface to standard mode
    hdmitx_writereg(display, HDMITX_DWC_I2CM_DIV, 0);

    // c. Setup i2c timings (based on u-boot source)
    hdmitx_writereg(display, HDMITX_DWC_I2CM_SS_SCL_HCNT_1, 0);
    hdmitx_writereg(display, HDMITX_DWC_I2CM_SS_SCL_HCNT_0, 0x67);
    hdmitx_writereg(display, HDMITX_DWC_I2CM_SS_SCL_LCNT_1, 0);
    hdmitx_writereg(display, HDMITX_DWC_I2CM_SS_SCL_LCNT_0, 0x78);
    hdmitx_writereg(display, HDMITX_DWC_I2CM_FS_SCL_HCNT_1, 0);
    hdmitx_writereg(display, HDMITX_DWC_I2CM_FS_SCL_HCNT_0, 0x0f);
    hdmitx_writereg(display, HDMITX_DWC_I2CM_FS_SCL_LCNT_1, 0);
    hdmitx_writereg(display, HDMITX_DWC_I2CM_FS_SCL_LCNT_0, 0x20);
    hdmitx_writereg(display, HDMITX_DWC_I2CM_SDA_HOLD, 0x08);

    // d. disable any SCDC operations for now
    hdmitx_writereg(display, HDMITX_DWC_I2CM_SCDC_UPDATE, 0);
    DISP_INFO("done!!\n");
    return ZX_OK;
}

static void hdmi_config_csc(vim2_display_t* display, const struct hdmi_param* p) {
    uint8_t csc_coef_a1_msb;
    uint8_t csc_coef_a1_lsb;
    uint8_t csc_coef_a2_msb;
    uint8_t csc_coef_a2_lsb;
    uint8_t csc_coef_a3_msb;
    uint8_t csc_coef_a3_lsb;
    uint8_t csc_coef_a4_msb;
    uint8_t csc_coef_a4_lsb;
    uint8_t csc_coef_b1_msb;
    uint8_t csc_coef_b1_lsb;
    uint8_t csc_coef_b2_msb;
    uint8_t csc_coef_b2_lsb;
    uint8_t csc_coef_b3_msb;
    uint8_t csc_coef_b3_lsb;
    uint8_t csc_coef_b4_msb;
    uint8_t csc_coef_b4_lsb;
    uint8_t csc_coef_c1_msb;
    uint8_t csc_coef_c1_lsb;
    uint8_t csc_coef_c2_msb;
    uint8_t csc_coef_c2_lsb;
    uint8_t csc_coef_c3_msb;
    uint8_t csc_coef_c3_lsb;
    uint8_t csc_coef_c4_msb;
    uint8_t csc_coef_c4_lsb;
    uint8_t csc_scale;
    uint32_t hdmi_data;

    if (display->input_color_format == display->output_color_format) {
        // no need to convert
        hdmi_data = MC_FLOWCTRL_BYPASS_CSC;
    } else {
        // conversion will be needed
        hdmi_data = MC_FLOWCTRL_ENB_CSC;
    }
    hdmitx_writereg(display, HDMITX_DWC_MC_FLOWCTRL, hdmi_data);

    // Since we don't support 422 at this point, set csc_cfg to 0
    hdmitx_writereg(display, HDMITX_DWC_CSC_CFG, 0);

    // Co-efficient values are from DesignWare Core HDMI TX Video Datapath Application Note V2.1

    // First determine whether we need to convert or not
    if (display->input_color_format != display->output_color_format) {
        if (display->input_color_format == HDMI_COLOR_FORMAT_RGB) {
            // from RGB
            csc_coef_a1_msb = 0x25;
            csc_coef_a1_lsb = 0x91;
            csc_coef_a2_msb = 0x13;
            csc_coef_a2_lsb = 0x23;
            csc_coef_a3_msb = 0x07;
            csc_coef_a3_lsb = 0x4C;
            csc_coef_a4_msb = 0x00;
            csc_coef_a4_lsb = 0x00;
            csc_coef_b1_msb = 0xE5;
            csc_coef_b1_lsb = 0x34;
            csc_coef_b2_msb = 0x20;
            csc_coef_b2_lsb = 0x00;
            csc_coef_b3_msb = 0xFA;
            csc_coef_b3_lsb = 0xCC;
            if (display->color_depth == HDMI_COLOR_DEPTH_24B) {
                csc_coef_b4_msb = 0x02;
                csc_coef_b4_lsb = 0x00;
                csc_coef_c4_msb = 0x02;
                csc_coef_c4_lsb = 0x00;
            } else if (display->color_depth == HDMI_COLOR_DEPTH_30B) {
                csc_coef_b4_msb = 0x08;
                csc_coef_b4_lsb = 0x00;
                csc_coef_c4_msb = 0x08;
                csc_coef_c4_lsb = 0x00;
            } else if (display->color_depth == HDMI_COLOR_DEPTH_36B) {
                csc_coef_b4_msb = 0x20;
                csc_coef_b4_lsb = 0x00;
                csc_coef_c4_msb = 0x20;
                csc_coef_c4_lsb = 0x00;
            } else {
                csc_coef_b4_msb = 0x20;
                csc_coef_b4_lsb = 0x00;
                csc_coef_c4_msb = 0x20;
                csc_coef_c4_lsb = 0x00;
            }
            csc_coef_c1_msb = 0xEA;
            csc_coef_c1_lsb = 0xCD;
            csc_coef_c2_msb = 0xF5;
            csc_coef_c2_lsb = 0x33;
            csc_coef_c3_msb = 0x20;
            csc_coef_c3_lsb = 0x00;
            csc_scale = 0;
        } else {
            // to RGB
            csc_coef_a1_msb =0x10;
            csc_coef_a1_lsb =0x00;
            csc_coef_a2_msb =0xf4;
            csc_coef_a2_lsb =0x93;
            csc_coef_a3_msb =0xfa;
            csc_coef_a3_lsb =0x7f;
            csc_coef_b1_msb =0x10;
            csc_coef_b1_lsb =0x00;
            csc_coef_b2_msb =0x16;
            csc_coef_b2_lsb =0x6e;
            csc_coef_b3_msb =0x00;
            csc_coef_b3_lsb =0x00;
            if (display->color_depth == HDMI_COLOR_DEPTH_24B) {
                csc_coef_a4_msb = 0x00;
                csc_coef_a4_lsb = 0x87;
                csc_coef_b4_msb = 0xff;
                csc_coef_b4_lsb = 0x4d;
                csc_coef_c4_msb = 0xff;
                csc_coef_c4_lsb = 0x1e;
            } else if (display->color_depth == HDMI_COLOR_DEPTH_30B) {
                csc_coef_a4_msb = 0x02;
                csc_coef_a4_lsb = 0x1d;
                csc_coef_b4_msb = 0xfd;
                csc_coef_b4_lsb = 0x33;
                csc_coef_c4_msb = 0xfc;
                csc_coef_c4_lsb = 0x75;
            } else if (display->color_depth == HDMI_COLOR_DEPTH_36B) {
                csc_coef_a4_msb = 0x08;
                csc_coef_a4_lsb = 0x77;
                csc_coef_b4_msb = 0xf4;
                csc_coef_b4_lsb = 0xc9;
                csc_coef_c4_msb = 0xf1;
                csc_coef_c4_lsb = 0xd3;
            } else {
                csc_coef_a4_msb = 0x08;
                csc_coef_a4_lsb = 0x77;
                csc_coef_b4_msb = 0xf4;
                csc_coef_b4_lsb = 0xc9;
                csc_coef_c4_msb = 0xf1;
                csc_coef_c4_lsb = 0xd3;
            }
            csc_coef_b4_msb =0xff;
            csc_coef_b4_lsb =0x4d;
            csc_coef_c1_msb =0x10;
            csc_coef_c1_lsb =0x00;
            csc_coef_c2_msb =0x00;
            csc_coef_c2_lsb =0x00;
            csc_coef_c3_msb =0x1c;
            csc_coef_c3_lsb =0x5a;
            csc_coef_c4_msb =0xff;
            csc_coef_c4_lsb =0x1e;
            csc_scale = 2;
        }
    } else {
        // No conversion. re-write default values just in case
        csc_coef_a1_msb = 0x20;
        csc_coef_a1_lsb = 0x00;
        csc_coef_a2_msb = 0x00;
        csc_coef_a2_lsb = 0x00;
        csc_coef_a3_msb = 0x00;
        csc_coef_a3_lsb = 0x00;
        csc_coef_a4_msb = 0x00;
        csc_coef_a4_lsb = 0x00;
        csc_coef_b1_msb = 0x00;
        csc_coef_b1_lsb = 0x00;
        csc_coef_b2_msb = 0x20;
        csc_coef_b2_lsb = 0x00;
        csc_coef_b3_msb = 0x00;
        csc_coef_b3_lsb = 0x00;
        csc_coef_b4_msb = 0x00;
        csc_coef_b4_lsb = 0x00;
        csc_coef_c1_msb = 0x00;
        csc_coef_c1_lsb = 0x00;
        csc_coef_c2_msb = 0x00;
        csc_coef_c2_lsb = 0x00;
        csc_coef_c3_msb = 0x20;
        csc_coef_c3_lsb = 0x00;
        csc_coef_c4_msb = 0x00;
        csc_coef_c4_lsb = 0x00;
        csc_scale = 1;
    }

    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_A1_MSB, csc_coef_a1_msb);
    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_A1_LSB, csc_coef_a1_lsb);
    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_A2_MSB, csc_coef_a2_msb);
    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_A2_LSB, csc_coef_a2_lsb);
    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_A3_MSB, csc_coef_a3_msb);
    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_A3_LSB, csc_coef_a3_lsb);
    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_A4_MSB, csc_coef_a4_msb);
    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_A4_LSB, csc_coef_a4_lsb);
    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_B1_MSB, csc_coef_b1_msb);
    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_B1_LSB, csc_coef_b1_lsb);
    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_B2_MSB, csc_coef_b2_msb);
    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_B2_LSB, csc_coef_b2_lsb);
    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_B3_MSB, csc_coef_b3_msb);
    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_B3_LSB, csc_coef_b3_lsb);
    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_B4_MSB, csc_coef_b4_msb);
    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_B4_LSB, csc_coef_b4_lsb);
    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_C1_MSB, csc_coef_c1_msb);
    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_C1_LSB, csc_coef_c1_lsb);
    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_C2_MSB, csc_coef_c2_msb);
    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_C2_LSB, csc_coef_c2_lsb);
    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_C3_MSB, csc_coef_c3_msb);
    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_C3_LSB, csc_coef_c3_lsb);
    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_C4_MSB, csc_coef_c4_msb);
    hdmitx_writereg(display, HDMITX_DWC_CSC_COEF_C4_LSB, csc_coef_c4_lsb);

    hdmi_data = 0;
    hdmi_data |= CSC_SCALE_COLOR_DEPTH(display->color_depth);
    hdmi_data |= CSC_SCALE_CSCSCALE(csc_scale);
    hdmitx_writereg(display, HDMITX_DWC_CSC_SCALE, hdmi_data);

}

static void hdmi_config_encoder(vim2_display_t* display, const struct hdmi_param* p) {
    uint32_t h_begin, h_end;
    uint32_t v_begin, v_end;
    uint32_t hs_begin, hs_end;
    uint32_t vs_begin, vs_end;
    uint32_t vsync_adjust = 0;
    uint32_t active_lines, total_lines;
    uint32_t venc_total_pixels, venc_active_pixels, venc_fp, venc_hsync;


    active_lines = (p->timings.vactive / (1 + p->timings.interlace_mode));
    total_lines = (active_lines + p->timings.vblank0) +
                    ((active_lines + p->timings.vblank1)*p->timings.interlace_mode);

    venc_total_pixels =  (p->timings.htotal / (p->timings.pixel_repeat + 1)) *
                            (p->timings.venc_pixel_repeat + 1);

    venc_active_pixels = (p->timings.hactive / (p->timings.pixel_repeat + 1)) *
                            (p->timings.venc_pixel_repeat + 1);

    venc_fp = (p->timings.hfront / (p->timings.pixel_repeat + 1)) *
                            (p->timings.venc_pixel_repeat + 1);

    venc_hsync = (p->timings.hsync / (p->timings.pixel_repeat + 1)) *
                            (p->timings.venc_pixel_repeat + 1);


    SET_BIT32(VPU, VPU_ENCP_VIDEO_MODE, 1, 1, 14);      // DE Signal polarity
    WRITE32_REG(VPU, VPU_ENCP_VIDEO_HAVON_BEGIN, p->timings.hsync + p->timings.hback);
    WRITE32_REG(VPU, VPU_ENCP_VIDEO_HAVON_END, p->timings.hsync + p->timings.hback +
        p->timings.hactive - 1);

    WRITE32_REG(VPU, VPU_ENCP_VIDEO_VAVON_BLINE, p->timings.vsync + p->timings.vback);
    WRITE32_REG(VPU, VPU_ENCP_VIDEO_VAVON_ELINE, p->timings.vsync + p->timings.vback +
        p->timings.vactive - 1);

    WRITE32_REG(VPU, VPU_ENCP_VIDEO_HSO_BEGIN, 0);
    WRITE32_REG(VPU, VPU_ENCP_VIDEO_HSO_END, p->timings.hsync);

    WRITE32_REG(VPU, VPU_ENCP_VIDEO_VSO_BLINE, 0);
    WRITE32_REG(VPU, VPU_ENCP_VIDEO_VSO_ELINE, p->timings.vsync);

    // Below calculations assume no pixel repeat and progressive mode.
    // HActive Start/End
    h_begin = p->timings.hsync + p->timings.hback + 2;  // 2 is the HDMI Latency

    h_begin = h_begin % venc_total_pixels; // wrap around if needed
    h_end = h_begin + venc_active_pixels;
    h_end = h_end % venc_total_pixels;   // wrap around if needed
    WRITE32_REG(VPU, VPU_ENCP_DE_H_BEGIN, h_begin);
    WRITE32_REG(VPU, VPU_ENCP_DE_H_END, h_end);

    // VActive Start/End
    v_begin = p->timings.vsync + p->timings.vback;
    v_end = v_begin + active_lines;
    WRITE32_REG(VPU, VPU_ENCP_DE_V_BEGIN_EVEN, v_begin);
    WRITE32_REG(VPU, VPU_ENCP_DE_V_END_EVEN, v_end);

    if (p->timings.interlace_mode) {
        // TODO: Add support for interlace mdoe
        // We should not even get here
        DISP_ERROR("Interface mode not supported\n");
    }

    // HSync Timings
    hs_begin = h_end + venc_fp;
    if (hs_begin >= venc_total_pixels) {
        hs_begin -= venc_total_pixels;
        vsync_adjust = 1;
    }

    hs_end = hs_begin + venc_hsync;
    hs_end = hs_end % venc_total_pixels;
    WRITE32_REG(VPU, VPU_ENCP_DVI_HSO_BEGIN, hs_begin);
    WRITE32_REG(VPU, VPU_ENCP_DVI_HSO_END, hs_end);

    // VSync Timings
    if (v_begin >= (p->timings.vback + p->timings.vsync + (1 - vsync_adjust))) {
        vs_begin = v_begin - p->timings.vback - p->timings.vsync - (1 - vsync_adjust);
    } else {
        vs_begin = p->timings.vtotal + v_begin - p->timings.vback - p->timings.vsync -
                    (1 - vsync_adjust);
    }
    vs_end = vs_begin + p->timings.vsync;
    vs_end = vs_end % total_lines;

    WRITE32_REG(VPU, VPU_ENCP_DVI_VSO_BLINE_EVN, vs_begin);
    WRITE32_REG(VPU, VPU_ENCP_DVI_VSO_ELINE_EVN, vs_end);
    WRITE32_REG(VPU, VPU_ENCP_DVI_VSO_BEGIN_EVN, hs_begin);
    WRITE32_REG(VPU, VPU_ENCP_DVI_VSO_END_EVN, hs_begin);

    WRITE32_REG(VPU, VPU_HDMI_SETTING, 0);
    // hsync, vsync active high. output CbYCr (GRB)
    // TODO: output desired format is hardcoded here to CbYCr (GRB)
    WRITE32_REG(VPU, VPU_HDMI_SETTING, (p->timings.hpol << 2) | (p->timings.vpol << 3) | (4 << 5));

    if (p->timings.venc_pixel_repeat) {
        SET_BIT32(VPU, VPU_HDMI_SETTING, 1, 1, 8);
    }

    // Select ENCP data to HDMI
    SET_BIT32(VPU, VPU_HDMI_SETTING, 2, 2, 0);

    DISP_INFO("done\n");
}

static void hdmi_config_hdmitx(vim2_display_t* display, const struct hdmi_param* p) {
    uint32_t hdmi_data;

    // Output normal TMDS Data
    hdmi_data = (1 << 12);
    hdmitx_writereg(display, HDMITX_TOP_BIST_CNTL, hdmi_data);

    // setup video input mapping
    hdmi_data = 0;
    if (display->input_color_format == HDMI_COLOR_FORMAT_RGB) {
        switch (display->color_depth) {
            case HDMI_COLOR_DEPTH_24B:
                hdmi_data |=  TX_INVID0_VM_RGB444_8B;
                break;
            case HDMI_COLOR_DEPTH_30B:
                hdmi_data |=  TX_INVID0_VM_RGB444_10B;
                break;
            case HDMI_COLOR_DEPTH_36B:
                hdmi_data |=  TX_INVID0_VM_RGB444_12B;
                break;
            case HDMI_COLOR_DEPTH_48B:
            default:
                hdmi_data |=  TX_INVID0_VM_RGB444_16B;
                break;
        }
    } else if (display->input_color_format == HDMI_COLOR_FORMAT_444) {
        switch (display->color_depth) {
            case HDMI_COLOR_DEPTH_24B:
                hdmi_data |=  TX_INVID0_VM_YCBCR444_8B;
                break;
            case HDMI_COLOR_DEPTH_30B:
                hdmi_data |=  TX_INVID0_VM_YCBCR444_10B;
                break;
            case HDMI_COLOR_DEPTH_36B:
                hdmi_data |=  TX_INVID0_VM_YCBCR444_12B;
                break;
            case HDMI_COLOR_DEPTH_48B:
            default:
                hdmi_data |=  TX_INVID0_VM_YCBCR444_16B;
                break;
            }
    } else {
        DISP_ERROR("Unsupported format!\n");
        return;
    }
    hdmitx_writereg(display, HDMITX_DWC_TX_INVID0, hdmi_data);


    // Disable video input stuffing and zero-out related registers
    hdmitx_writereg(display, HDMITX_DWC_TX_INSTUFFING, 0x00);
    hdmitx_writereg(display, HDMITX_DWC_TX_GYDATA0, 0x00);
    hdmitx_writereg(display, HDMITX_DWC_TX_GYDATA1, 0x00);
    hdmitx_writereg(display, HDMITX_DWC_TX_RCRDATA0, 0x00);
    hdmitx_writereg(display, HDMITX_DWC_TX_RCRDATA1, 0x00);
    hdmitx_writereg(display, HDMITX_DWC_TX_BCBDATA0, 0x00);
    hdmitx_writereg(display, HDMITX_DWC_TX_BCBDATA1, 0x00);

    // configure CSC (Color Space Converter)
    hdmi_config_csc(display, p);

    // Video packet color depth and pixel repetition (none). writing 0 is also valid
    // hdmi_data = (4 << 4); // 4 == 24bit
    // hdmi_data = (display->color_depth << 4); // 4 == 24bit
    hdmi_data = (0 << 4); // 4 == 24bit
    hdmitx_writereg(display, HDMITX_DWC_VP_PR_CD, hdmi_data);

    // setup video packet stuffing (nothing fancy to be done here)
    hdmi_data = 0;
    hdmitx_writereg(display, HDMITX_DWC_VP_STUFF, hdmi_data);

    // setup video packet remap (nothing here as well since we don't support 422)
    hdmi_data = 0;
    hdmitx_writereg(display, HDMITX_DWC_VP_REMAP, hdmi_data);

    // vp packet output configuration
    // hdmi_data = 0;
    hdmi_data = VP_CONF_BYPASS_EN;
    hdmi_data |= VP_CONF_BYPASS_SEL_VP;
    hdmi_data |= VP_CONF_OUTSELECTOR;
    hdmitx_writereg(display, HDMITX_DWC_VP_CONF, hdmi_data);

    // Video packet Interrupt Mask
    hdmi_data = 0xFF; // set all bits
    hdmitx_writereg(display, HDMITX_DWC_VP_MASK, hdmi_data);

    // TODO: For now skip audio configuration

    // Setup frame composer

    // fc_invidconf setup
    hdmi_data = 0;
    hdmi_data |= FC_INVIDCONF_HDCP_KEEPOUT;
    hdmi_data |= FC_INVIDCONF_VSYNC_POL(p->timings.vpol);
    hdmi_data |= FC_INVIDCONF_HSYNC_POL(p->timings.hpol);
    hdmi_data |= FC_INVIDCONF_DE_POL_H;
    hdmi_data |= FC_INVIDCONF_DVI_HDMI_MODE;
    if (p->timings.interlace_mode) {
        hdmi_data |= FC_INVIDCONF_VBLANK_OSC | FC_INVIDCONF_IN_VID_INTERLACED;
    }
    hdmitx_writereg(display, HDMITX_DWC_FC_INVIDCONF, hdmi_data);

    // HActive
    hdmi_data = p->timings.hactive;
    hdmitx_writereg(display, HDMITX_DWC_FC_INHACTV0, (hdmi_data & 0xff));
    hdmitx_writereg(display, HDMITX_DWC_FC_INHACTV1, ((hdmi_data >> 8) & 0x3f));

    // HBlank
    hdmi_data = p->timings.hblank;
    hdmitx_writereg(display, HDMITX_DWC_FC_INHBLANK0, (hdmi_data & 0xff));
    hdmitx_writereg(display, HDMITX_DWC_FC_INHBLANK1, ((hdmi_data >> 8) & 0x1f));

    // VActive
    hdmi_data = p->timings.vactive;
    hdmitx_writereg(display, HDMITX_DWC_FC_INVACTV0, (hdmi_data & 0xff));
    hdmitx_writereg(display, HDMITX_DWC_FC_INVACTV1, ((hdmi_data >> 8) & 0x1f));

    // VBlank
    hdmi_data = p->timings.vblank0;
    hdmitx_writereg(display, HDMITX_DWC_FC_INVBLANK, (hdmi_data & 0xff));

    // HFP
    hdmi_data = p->timings.hfront;
    hdmitx_writereg(display, HDMITX_DWC_FC_HSYNCINDELAY0, (hdmi_data & 0xff));
    hdmitx_writereg(display, HDMITX_DWC_FC_HSYNCINDELAY1, ((hdmi_data >> 8) & 0x1f));

    // HSync
    hdmi_data = p->timings.hsync;
    hdmitx_writereg(display, HDMITX_DWC_FC_HSYNCINWIDTH0, (hdmi_data & 0xff));
    hdmitx_writereg(display, HDMITX_DWC_FC_HSYNCINWIDTH1, ((hdmi_data >> 8) & 0x3));

    // VFront
    hdmi_data = p->timings.vfront;
    hdmitx_writereg(display, HDMITX_DWC_FC_VSYNCINDELAY, (hdmi_data & 0xff));

    //VSync
    hdmi_data = p->timings.vsync;
    hdmitx_writereg(display, HDMITX_DWC_FC_VSYNCINWIDTH, (hdmi_data & 0x3f));

    // Frame Composer control period duration (set to 12 per spec)
    hdmitx_writereg(display, HDMITX_DWC_FC_CTRLDUR, 12);

    // Frame Composer extended control period duration (set to 32 per spec)
    hdmitx_writereg(display, HDMITX_DWC_FC_EXCTRLDUR, 32);

    // Frame Composer extended control period max spacing (FIXME: spec says 50, uboot sets to 1)
    hdmitx_writereg(display, HDMITX_DWC_FC_EXCTRLSPAC, 1);

    // Frame Composer preamble filler (from uBoot)

    // Frame Composer GCP packet config
    hdmi_data = (1 << 1); // set avmute. defauly_phase is 0
    hdmitx_writereg(display, HDMITX_DWC_FC_GCP, hdmi_data);

    // Frame Composer AVI Packet config (set active_format_present bit)
    // aviconf0 populates Table 10 of CEA spec (AVI InfoFrame Data Byte 1)
    // Y1Y0 = 00 for RGB, 10 for 444
    if (display->output_color_format == HDMI_COLOR_FORMAT_RGB) {
        hdmi_data = FC_AVICONF0_RGB;
    } else {
        hdmi_data = FC_AVICONF0_444;
    }
    // A0 = 1 Active Formate present on R3R0
    hdmi_data |= FC_AVICONF0_A0;
    hdmitx_writereg(display, HDMITX_DWC_FC_AVICONF0, hdmi_data);

    // aviconf1 populates Table 11 of AVI InfoFrame Data Byte 2
    // C1C0 = 0, M1M0=0x2 (16:9), R3R2R1R0=0x8 (same of M1M0)
    hdmi_data = FC_AVICONF1_R3R0; // set to 0x8 (same as coded frame aspect ratio)
    hdmi_data |= FC_AVICONF1_M1M0(p->aspect_ratio);
    hdmi_data |= FC_AVICONF1_C1C0(p->colorimetry);
    hdmitx_writereg(display, HDMITX_DWC_FC_AVICONF1, hdmi_data);

    // Since we are support RGB/444, no need to write to ECx
    hdmitx_writereg(display, HDMITX_DWC_FC_AVICONF2, 0x0);

    // YCC and IT Quantizations according to CEA spec (limited range for now)
    hdmitx_writereg(display, HDMITX_DWC_FC_AVICONF3, 0x0);

    // Set AVI InfoFrame VIC
    // hdmitx_writereg(display, HDMITX_DWC_FC_AVIVID, (p->vic >= VESA_OFFSET)? 0 : p->vic);

    hdmitx_writereg(display, HDMITX_DWC_FC_ACTSPC_HDLR_CFG, 0);

    // Frame composer 2d vact config
    hdmi_data = p->timings.vactive;
    hdmitx_writereg(display, HDMITX_DWC_FC_INVACT_2D_0, (hdmi_data & 0xff));
    hdmitx_writereg(display, HDMITX_DWC_FC_INVACT_2D_1, ((hdmi_data >> 8) & 0xf));

    // disable all Frame Composer interrupts
    hdmitx_writereg(display, HDMITX_DWC_FC_MASK0, 0xe7);
    hdmitx_writereg(display, HDMITX_DWC_FC_MASK1, 0xfb);
    hdmitx_writereg(display, HDMITX_DWC_FC_MASK2, 0x3);

    // No pixel repeition for the currently supported resolution
    hdmitx_writereg(display, HDMITX_DWC_FC_PRCONF,
                                ((p->timings.pixel_repeat + 1) << 4) |
                                (p->timings.pixel_repeat) << 0);

    // Skip HDCP for now

    // Clear Interrupts
    hdmitx_writereg(display, HDMITX_DWC_IH_FC_STAT0,  0xff);
    hdmitx_writereg(display, HDMITX_DWC_IH_FC_STAT1,  0xff);
    hdmitx_writereg(display, HDMITX_DWC_IH_FC_STAT2,  0xff);
    hdmitx_writereg(display, HDMITX_DWC_IH_AS_STAT0,  0xff);
    hdmitx_writereg(display, HDMITX_DWC_IH_PHY_STAT0, 0xff);
    hdmitx_writereg(display, HDMITX_DWC_IH_I2CM_STAT0, 0xff);
    hdmitx_writereg(display, HDMITX_DWC_IH_CEC_STAT0, 0xff);
    hdmitx_writereg(display, HDMITX_DWC_IH_VP_STAT0,  0xff);
    hdmitx_writereg(display, HDMITX_DWC_IH_I2CMPHY_STAT0, 0xff);
    hdmitx_writereg(display, HDMITX_DWC_A_APIINTCLR,  0xff);
    hdmitx_writereg(display, HDMITX_DWC_HDCP22REG_STAT, 0xff);

    hdmitx_writereg(display, HDMITX_TOP_INTR_STAT_CLR, 0x0000001f);

    // setup interrupts we care about
    hdmitx_writereg(display, HDMITX_DWC_IH_MUTE_FC_STAT0, 0xff);
    hdmitx_writereg(display, HDMITX_DWC_IH_MUTE_FC_STAT1, 0xff);
    hdmitx_writereg(display, HDMITX_DWC_IH_MUTE_FC_STAT2, 0x3);

    hdmitx_writereg(display, HDMITX_DWC_IH_MUTE_AS_STAT0, 0x7); // mute all

    hdmitx_writereg(display, HDMITX_DWC_IH_MUTE_PHY_STAT0, 0x3f);

    hdmi_data = (1 << 1); // mute i2c master done.
    hdmitx_writereg(display, HDMITX_DWC_IH_MUTE_I2CM_STAT0, hdmi_data);

    // turn all cec-related interrupts on
    hdmitx_writereg(display, HDMITX_DWC_IH_MUTE_CEC_STAT0, 0x0);

    hdmitx_writereg(display, HDMITX_DWC_IH_MUTE_VP_STAT0, 0xff);

    hdmitx_writereg(display, HDMITX_DWC_IH_MUTE_I2CMPHY_STAT0, 0x03);

    // enable global interrupt
    hdmitx_writereg(display, HDMITX_DWC_IH_MUTE, 0x0);

    hdmitx_writereg(display, HDMITX_TOP_INTR_MASKN, 0x1f);

    // reset
    hdmitx_writereg(display, HDMITX_DWC_MC_SWRSTZREQ, 0x00);
    usleep(10);
    hdmitx_writereg(display, HDMITX_DWC_MC_SWRSTZREQ, 0xdd);
    // why???
   hdmitx_writereg(display, HDMITX_DWC_FC_VSYNCINWIDTH, hdmitx_readreg(display, HDMITX_DWC_FC_VSYNCINWIDTH));

    // dump_regs(display);
    DISP_INFO("done\n");
}

static void hdmi_config_phy(vim2_display_t* display, const struct hdmi_param* p) {

    WRITE32_REG(HHI, HHI_HDMI_PHY_CNTL0, 0);
    SET_BIT32(HHI, HHI_HDMI_PHY_CNTL1, 0x0390, 16, 16);
    SET_BIT32(HHI, HHI_HDMI_PHY_CNTL1, 0x0, 4, 0);

    SET_BIT32(HHI, HHI_HDMI_PHY_CNTL1, 0xf, 4, 0);
    usleep(2);
    SET_BIT32(HHI, HHI_HDMI_PHY_CNTL1, 0xe, 4, 0);
    usleep(2);
    SET_BIT32(HHI, HHI_HDMI_PHY_CNTL1, 0xf, 4, 0);
    usleep(2);
    SET_BIT32(HHI, HHI_HDMI_PHY_CNTL1, 0xe, 4, 0);
    usleep(2);

    switch (p->phy_mode) {
        case 1: /* 5.94Gbps, 3.7125Gbsp */
            WRITE32_REG(HHI, HHI_HDMI_PHY_CNTL0, 0x333d3282);
            WRITE32_REG(HHI, HHI_HDMI_PHY_CNTL3, 0x2136315b);
            break;
        case 2: /* 2.97Gbps */
            WRITE32_REG(HHI, HHI_HDMI_PHY_CNTL0, 0x33303382);
            WRITE32_REG(HHI, HHI_HDMI_PHY_CNTL3, 0x2036315b);
            break;
        case 3: /* 1.485Gbps */
            WRITE32_REG(HHI, HHI_HDMI_PHY_CNTL0, 0x33303042);
            WRITE32_REG(HHI, HHI_HDMI_PHY_CNTL3, 0x2016315b);
            break;
        default: /* 742.5Mbps, and below */
            WRITE32_REG(HHI, HHI_HDMI_PHY_CNTL0, 0x33604132);
            WRITE32_REG(HHI, HHI_HDMI_PHY_CNTL3, 0x0016315b);
            break;

    }
    usleep(20);
    DISP_INFO("done!\n");
}

#if 0
/* FIXME: Write better HDMI Test functions
 * Leave this function commented out for now
 */
void hdmi_test(vim2_display_t* display, uint32_t width) {
    SET_BIT32(VPU, VPU_ENCP_VIDEO_MODE_ADV, 0, 1, 3);
    WRITE32_REG(VPU, VPU_VENC_VIDEO_TST_EN, 1);
    DISP_INFO("width = %d\n", width);
    while (1) {
        // (107, 202, 222)
        WRITE32_REG(VPU, VPU_VENC_VIDEO_TST_MDSEL, 0);
        WRITE32_REG(VPU, VPU_VENC_VIDEO_TST_Y, 107);
        WRITE32_REG(VPU, VPU_VENC_VIDEO_TST_CB, 202);
        WRITE32_REG(VPU, VPU_VENC_VIDEO_TST_CR, 222);
        sleep(5);
        WRITE32_REG(VPU, VPU_VENC_VIDEO_TST_MDSEL, 1);
        WRITE32_REG(VPU, VPU_VENC_VIDEO_TST_CLRBAR_WIDTH, 250);
        WRITE32_REG(VPU, VPU_VENC_VIDEO_TST_CLRBAR_STRT, 0);
        sleep(5);
        WRITE32_REG(VPU, VPU_VENC_VIDEO_TST_MDSEL, 2);
        sleep(5);
        WRITE32_REG(VPU, VPU_VENC_VIDEO_TST_MDSEL, 3);
        sleep(5);
    }
}
#endif

zx_status_t init_hdmi_interface(vim2_display_t* display, const struct hdmi_param* p) {

    uint8_t scdc_data = 0;
    uint32_t regval = 0;

    // FIXME: Need documentation for HDMI PLL initialization
    configure_pll(display, p, &p->pll_p_24b);

    for (size_t i = 0; ENC_LUT_GEN[i].reg != 0xFFFFFFFF; i++) {
        WRITE32_REG(VPU, ENC_LUT_GEN[i].reg, ENC_LUT_GEN[i].val);
    }

    WRITE32_REG(VPU, VPU_ENCP_VIDEO_MAX_PXCNT, (p->timings.venc_pixel_repeat)?
                ((p->timings.htotal << 1) - 1) : (p->timings.htotal - 1));
    WRITE32_REG(VPU, VPU_ENCP_VIDEO_MAX_LNCNT, p->timings.vtotal - 1);

    if (p->timings.venc_pixel_repeat) {
        SET_BIT32(VPU, VPU_ENCP_VIDEO_MODE_ADV, 1, 1, 0);
    }

    // Configure Encoder with detailed timing info (based on resolution)
    hdmi_config_encoder(display, p);

    // Configure VDAC
    WRITE32_REG(HHI, HHI_VDAC_CNTL0, 0);
    WRITE32_REG(HHI, HHI_VDAC_CNTL1, 8); // set Cdac_pwd [whatever that is]

    // Configure HDMI TX IP
    hdmi_config_hdmitx(display, p);

    if (p->is4K) {
        // Setup TMDS Clocks (magic numbers)
        hdmitx_writereg(display, HDMITX_TOP_TMDS_CLK_PTTN_01, 0);
        hdmitx_writereg(display, HDMITX_TOP_TMDS_CLK_PTTN_23, 0x03ff03ff);
        hdmitx_writereg(display, HDMITX_DWC_FC_SCRAMBLER_CTRL,
            hdmitx_readreg(display, HDMITX_DWC_FC_SCRAMBLER_CTRL) | (1 << 0));
    } else {
        hdmitx_writereg(display, HDMITX_TOP_TMDS_CLK_PTTN_01, 0x001f001f);
        hdmitx_writereg(display, HDMITX_TOP_TMDS_CLK_PTTN_23, 0x001f001f);
        hdmitx_writereg(display, HDMITX_DWC_FC_SCRAMBLER_CTRL, 0);
    }

    hdmitx_writereg(display, HDMITX_TOP_TMDS_CLK_PTTN_CNTL, 0x1);
    usleep(2);
    hdmitx_writereg(display, HDMITX_TOP_TMDS_CLK_PTTN_CNTL, 0x2);

    hdmi_scdc_read(display, 0x1, &scdc_data);
    DISP_INFO("version is %s\n", (scdc_data == 1)? "2.0" : "<= 1.4");
    // scdc write is done twice in uboot
    // TODO: find scdc register def
    hdmi_scdc_write(display, 0x2, 0x1);
    hdmi_scdc_write(display, 0x2, 0x1);

    if (p->is4K) {
        hdmi_scdc_write(display, 0x20, 3);
        hdmi_scdc_write(display, 0x20, 3);
    } else {
        hdmi_scdc_write(display, 0x20, 0);
        hdmi_scdc_write(display, 0x20, 0);
    }

    // Setup HDMI related registers in VPU

    // not really needed since we are not converting from 420/422. but set anyways
    WRITE32_REG(VPU, VPU_HDMI_FMT_CTRL, (2 << 2));

    // setup some magic registers
    SET_BIT32(VPU, VPU_HDMI_FMT_CTRL, 0, 1, 4);
    SET_BIT32(VPU, VPU_HDMI_FMT_CTRL, 1, 1, 10);
    SET_BIT32(VPU, VPU_HDMI_DITH_CNTL, 1, 1, 4);
    SET_BIT32(VPU, VPU_HDMI_DITH_CNTL, 0, 2, 2);

    // reset vpu bridge
    regval = (READ32_REG(VPU, VPU_HDMI_SETTING) & 0xf00) >> 8;
    WRITE32_REG(VPU, VPU_ENCP_VIDEO_EN, 0);
    SET_BIT32(VPU, VPU_HDMI_SETTING, 0, 2, 0); // disable hdmi source
    SET_BIT32(VPU, VPU_HDMI_SETTING, 0, 4, 8); // why???
    usleep(1);
    WRITE32_REG(VPU, VPU_ENCP_VIDEO_EN, 1);
    usleep(1);
    SET_BIT32(VPU, VPU_HDMI_SETTING, regval, 4, 8); // why???
    usleep(1);
    SET_BIT32(VPU, VPU_HDMI_SETTING, 2, 2, 0); // select encp data to hdmi

    regval = hdmitx_readreg(display, HDMITX_DWC_FC_INVIDCONF);
    regval &= ~(1 << 3); // clear hdmi mode select
    hdmitx_writereg(display,HDMITX_DWC_FC_INVIDCONF, regval);
    usleep(1);
    regval = hdmitx_readreg(display, HDMITX_DWC_FC_INVIDCONF);
    regval |= (1 << 3); // clear hdmi mode select
    hdmitx_writereg(display,HDMITX_DWC_FC_INVIDCONF, regval);
    usleep(1);

    // setup hdmi phy
    hdmi_config_phy(display, p);
    hdmitx_writereg(display, HDMITX_DWC_FC_GCP, (1 << 0));

    DISP_INFO("done!!\n");
    return ZX_OK;
}

void dump_regs(vim2_display_t* display)
{
    unsigned int reg_adr;
    unsigned int reg_val;
    unsigned int ladr;
    for (reg_adr = 0x0000; reg_adr < 0x0100; reg_adr ++) {
                ladr = (reg_adr << 2);
        reg_val = READ32_REG(HHI, ladr);
        DISP_INFO("[0x%08x] = 0x%X\n", ladr, reg_val);
    }
#define VPU_REG_ADDR(reg) ((reg << 2))
    for (reg_adr = 0x1b00; reg_adr < 0x1c00; reg_adr ++) {
        ladr = VPU_REG_ADDR(reg_adr);
        reg_val = READ32_REG(VPU, ladr);
        DISP_INFO("[0x%08x] = 0x%X\n", ladr, reg_val);
    }
    for (reg_adr = 0x1c01; reg_adr < 0x1d00; reg_adr ++) {
        ladr = VPU_REG_ADDR(reg_adr);
        reg_val = READ32_REG(VPU, ladr);
        DISP_INFO("[0x%08x] = 0x%X\n", ladr, reg_val);
    }
    for (reg_adr = 0x2700; reg_adr < 0x2780; reg_adr ++) {
        ladr = VPU_REG_ADDR(reg_adr);
        reg_val = READ32_REG(VPU, ladr);
        DISP_INFO("[0x%08x] = 0x%X\n", ladr, reg_val);
    }
    for (reg_adr = HDMITX_TOP_SW_RESET; reg_adr < HDMITX_TOP_STAT0 + 1; reg_adr ++) {
        reg_val = hdmitx_readreg(display, reg_adr);
        DISP_INFO("TOP[0x%x]: 0x%x\n", reg_adr, reg_val);
    }
    for (reg_adr = HDMITX_DWC_DESIGN_ID; reg_adr < HDMITX_DWC_I2CM_SCDC_UPDATE1 + 1; reg_adr ++) {
        if ((reg_adr > HDMITX_DWC_HDCP_BSTATUS_0 -1) && (reg_adr < HDMITX_DWC_HDCPREG_BKSV0)) {
            reg_val = 0;
        } else {
            reg_val = hdmitx_readreg(display, reg_adr);
        }
        if (reg_val) {
            // excluse HDCP regisiters
            if ((reg_adr < HDMITX_DWC_A_HDCPCFG0) || (reg_adr > HDMITX_DWC_CEC_CTRL))
                DISP_INFO("DWC[0x%x]: 0x%x\n", reg_adr, reg_val);
        }
    }
}
