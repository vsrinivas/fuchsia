// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-device.h>
#include <zircon/listnode.h>
#include <zircon/types.h>
#include <threads.h>
#include "vpu.h"
#include "dwc-hdmi.h"

#define DISPLAY_MASK(start, count) (((1 << (count)) - 1) << (start))
#define DISPLAY_SET_MASK(mask, start, count, value) \
                        ((mask & ~DISPLAY_MASK(start, count)) | \
                                (((value) << (start)) & DISPLAY_MASK(start, count)))

#define READ32_PRESET_REG(a)             readl(io_buffer_virt(&display->mmio_preset) + a)
#define WRITE32_PRESET_REG(a, v)         writel(v, io_buffer_virt(&display->mmio_preset) + a)

#define READ32_HDMITX_REG(a)             readl(io_buffer_virt(&display->mmio_hdmitx) + a)
#define WRITE32_HDMITX_REG(a, v)         writel(v, io_buffer_virt(&display->mmio_hdmitx) + a)

#define READ32_HHI_REG(a)                readl(io_buffer_virt(&display->mmio_hiu) + a)
#define WRITE32_HHI_REG(a, v)            writel(v, io_buffer_virt(&display->mmio_hiu) + a)

#define READ32_VPU_REG(a)                readl(io_buffer_virt(&display->mmio_vpu) + a)
#define WRITE32_VPU_REG(a, v)            writel(v, io_buffer_virt(&display->mmio_vpu) + a)

#define READ32_DMC_REG(a)                readl(io_buffer_virt(&display->mmio_dmc) + a)
#define WRITE32_DMC_REG(a, v)            writel(v, io_buffer_virt(&display->mmio_dmc) + a)

#define READ32_HDMITX_SEC_REG(a)         readl(io_buffer_virt(&display->mmio_hdmitx_sec) + a)
#define WRITE32_HDMITX_SEC_REG(a, v)     writel(v, io_buffer_virt(&display->mmio_hdmitx_sec) + a)

#define READ32_CBUS_REG(a)              readl(io_buffer_virt(&display->mmio_cbus) + 0x400 + a)
#define WRITE32_CBUS_REG(a, v)          writel(v, io_buffer_virt(&display->mmio_cbus) + 0x400+ a)

#define SET_BIT32(x, dest, value, count, start) \
            WRITE32_##x##_REG(dest, (READ32_##x##_REG(dest) & ~DISPLAY_MASK(start, count)) | \
                                (((value) << (start)) & DISPLAY_MASK(start, count)))

#define WRITE32_REG(x, a, v)    WRITE32_##x##_REG(a, v)
#define READ32_REG(x, a)        READ32_##x##_REG(a)

#define SEC_OFFSET           (0x1UL << 24)
#define TOP_OFFSET_MASK      (0x0UL << 24)
#define TOP_SEC_OFFSET_MASK  ((TOP_OFFSET_MASK) | (SEC_OFFSET))
#define DWC_OFFSET_MASK      (0x10UL << 24)
#define DWC_SEC_OFFSET_MASK  ((DWC_OFFSET_MASK) | (SEC_OFFSET))

#define DMC_CAV_LUT_DATAL           (0x12 << 2)
#define DMC_CAV_LUT_DATAH           (0x13 << 2)
#define DMC_CAV_LUT_ADDR             (0x14 << 2)

#define DMC_CAV_ADDR_LMASK       0x1fffffff
#define DMC_CAV_WIDTH_LMASK      0x7
#define DMC_CAV_WIDTH_LWID       3
#define DMC_CAV_WIDTH_LBIT       29

#define DMC_CAV_WIDTH_HMASK      0x1ff
#define DMC_CAV_WIDTH_HBIT       0
#define DMC_CAV_HEIGHT_MASK      0x1fff
#define DMC_CAV_HEIGHT_BIT       9

#define DMC_CAV_LUT_ADDR_INDEX_MASK  0x7
#define DMC_CAV_LUT_ADDR_RD_EN       (1 << 8)
#define DMC_CAV_LUT_ADDR_WR_EN       (2 << 8)


// P RESET
#define PRESET_REGISTER                                 (0x400)
#define PRESET0_REGISTER                                (0x404)
#define PRESET2_REGISTER                                (0x40C)

// HDMITX ADDRESS and DATA PORTS
#define HDMITX_ADDR_PORT                                (0x00)
#define HDMITX_DATA_PORT                                (0x04)
#define HDMITX_CTRL_PORT                                (0x08)


// HDMI TOP
#define HDMITX_TOP_SW_RESET                             (TOP_OFFSET_MASK + 0x000)
#define HDMITX_TOP_CLK_CNTL                             (TOP_OFFSET_MASK + 0x001)
#define HDMITX_TOP_HPD_FILTER                           (TOP_OFFSET_MASK + 0x002)
#define HDMITX_TOP_INTR_MASKN                           (TOP_OFFSET_MASK + 0x003)
#define HDMITX_TOP_INTR_STAT                            (TOP_OFFSET_MASK + 0x004)
#define HDMITX_TOP_INTR_STAT_CLR                        (TOP_OFFSET_MASK + 0x005)
#define HDMITX_TOP_BIST_CNTL                            (TOP_OFFSET_MASK + 0x006)
#define HDMITX_TOP_SHIFT_PTTN_012                       (TOP_OFFSET_MASK + 0x007)
#define HDMITX_TOP_SHIFT_PTTN_345                       (TOP_OFFSET_MASK + 0x008)
#define HDMITX_TOP_SHIFT_PTTN_67                        (TOP_OFFSET_MASK + 0x009)
#define HDMITX_TOP_TMDS_CLK_PTTN_01                     (TOP_OFFSET_MASK + 0x00A)
#define HDMITX_TOP_TMDS_CLK_PTTN_23                     (TOP_OFFSET_MASK + 0x00B)
#define HDMITX_TOP_TMDS_CLK_PTTN_CNTL                   (TOP_OFFSET_MASK + 0x00C)
#define HDMITX_TOP_REVOCMEM_STAT                        (TOP_OFFSET_MASK + 0x00D)
#define HDMITX_TOP_STAT0                                (TOP_OFFSET_MASK + 0x00E)
#define HDMITX_TOP_SKP_CNTL_STAT                        (TOP_SEC_OFFSET_MASK + 0x010)
#define HDMITX_TOP_NONCE_0                              (TOP_SEC_OFFSET_MASK + 0x011)
#define HDMITX_TOP_NONCE_1                              (TOP_SEC_OFFSET_MASK + 0x012)
#define HDMITX_TOP_NONCE_2                              (TOP_SEC_OFFSET_MASK + 0x013)
#define HDMITX_TOP_NONCE_3                              (TOP_SEC_OFFSET_MASK + 0x014)
#define HDMITX_TOP_PKF_0                                (TOP_SEC_OFFSET_MASK + 0x015)
#define HDMITX_TOP_PKF_1                                (TOP_SEC_OFFSET_MASK + 0x016)
#define HDMITX_TOP_PKF_2                                (TOP_SEC_OFFSET_MASK + 0x017)
#define HDMITX_TOP_PKF_3                                (TOP_SEC_OFFSET_MASK + 0x018)
#define HDMITX_TOP_DUK_0                                (TOP_SEC_OFFSET_MASK + 0x019)
#define HDMITX_TOP_DUK_1                                (TOP_SEC_OFFSET_MASK + 0x01A)
#define HDMITX_TOP_DUK_2                                (TOP_SEC_OFFSET_MASK + 0x01B)
#define HDMITX_TOP_DUK_3                                (TOP_SEC_OFFSET_MASK + 0x01C)
#define HDMITX_TOP_INFILTER                             (TOP_OFFSET_MASK + 0x01D)
#define HDMITX_TOP_NSEC_SCRATCH                         (TOP_OFFSET_MASK + 0x01E)
#define HDMITX_TOP_SEC_SCRATCH                          (TOP_SEC_OFFSET_MASK + 0x01F)
#define HDMITX_TOP_DONT_TOUCH0                          (TOP_OFFSET_MASK + 0x0FE)
#define HDMITX_TOP_DONT_TOUCH1                          (TOP_OFFSET_MASK + 0x0FF)




#define PAD_PULL_UP_EN_REG1                             (0x49 << 2)
#define PAD_PULL_UP_REG1                                (0x3d << 2)
#define P_PREG_PAD_GPIO1_EN_N                           (0x0f << 2)
#define PERIPHS_PIN_MUX_6                               (0x32 << 2)

struct reg_val_pair {
    uint32_t    reg;
    uint32_t    val;
};

static const struct reg_val_pair ENC_LUT_GEN[] = {
    {VPU_ENCP_VIDEO_EN, 0,},
    {VPU_ENCI_VIDEO_EN, 0,},
    {VPU_ENCP_VIDEO_MODE, 0x4040,},
    {VPU_ENCP_VIDEO_MODE_ADV, 0x18,},
    {VPU_VPU_VIU_VENC_MUX_CTRL, 0xA},
    {VPU_ENCP_VIDEO_VSO_BEGIN, 16},
    {VPU_ENCP_VIDEO_VSO_END, 32},
    {VPU_ENCI_VIDEO_EN, 0},
    {VPU_ENCP_VIDEO_EN, 1},
    { 0xFFFFFFFF, 0},
};

struct cea_timing {
    uint8_t             interlace_mode;
    uint32_t            pfreq;
    uint8_t             ln;
    uint8_t             pixel_repeat;
    uint8_t             venc_pixel_repeat;

    uint32_t            hfreq;
    uint32_t            hactive;
    uint32_t            htotal;
    uint32_t            hblank;
    uint32_t            hfront;
    uint32_t            hsync;
    uint32_t            hback;
    uint8_t             hpol;

    uint32_t            vfreq;
    uint32_t            vactive;
    uint32_t            vtotal;
    uint32_t            vblank0;    // in case of interlace
    uint32_t            vblank1;    // vblank0 + 1 for interlace
    uint32_t            vfront;
    uint32_t            vsync;
    uint32_t            vback;
    uint8_t             vpol;
};

#define VID_PLL_DIV_1      0
#define VID_PLL_DIV_2      1
#define VID_PLL_DIV_3      2
#define VID_PLL_DIV_3p5    3
#define VID_PLL_DIV_3p75   4
#define VID_PLL_DIV_4      5
#define VID_PLL_DIV_5      6
#define VID_PLL_DIV_6      7
#define VID_PLL_DIV_6p25   8
#define VID_PLL_DIV_7      9
#define VID_PLL_DIV_7p5    10
#define VID_PLL_DIV_12     11
#define VID_PLL_DIV_14     12
#define VID_PLL_DIV_15     13
#define VID_PLL_DIV_2p5    14

enum viu_type {
    VIU_ENCL = 0,
    VIU_ENCI,
    VIU_ENCP,
    VIU_ENCT,
};

struct pll_param {
    uint32_t mode;
    uint32_t viu_channel;
    uint32_t viu_type;
    uint32_t hpll_clk_out;
    uint32_t od1;
    uint32_t od2;
    uint32_t od3;
    uint32_t vid_pll_div;
    uint32_t vid_clk_div;
    uint32_t hdmi_tx_pixel_div;
    uint32_t encp_div;
    uint32_t enci_div;
};

struct hdmi_param {
    uint16_t                        vic;
    uint8_t                         aspect_ratio;
    uint8_t                         colorimetry;
    uint8_t                         phy_mode;
    struct pll_param                pll_p_24b;
    struct cea_timing               timings;
    bool                            is4K;
};

#define HDMI_COLOR_DEPTH_24B    4
#define HDMI_COLOR_DEPTH_30B    5
#define HDMI_COLOR_DEPTH_36B    6
#define HDMI_COLOR_DEPTH_48B    7

#define HDMI_COLOR_FORMAT_RGB   0
#define HDMI_COLOR_FORMAT_444   1

#define HDMI_ASPECT_RATIO_4x3   1
#define HDMI_ASPECT_RATIO_16x9  2

#define HDMI_COLORIMETRY_ITU601  1
#define HDMI_COLORIMETRY_ITU709  2

/* VIC lookup */
 #define VIC_720x480p_60Hz_4x3                  2
 #define VIC_720x480p_60Hz_16x9                 3
 #define VIC_1280x720p_60Hz_16x9                4
 #define VIC_1920x1080i_60Hz_16x9               5
 #define VIC_720x480i_60Hz_4x3                  6
 #define VIC_720x480i_60Hz_16x9                 7
 #define VIC_720x240p_60Hz_4x3                  8
 #define VIC_720x240p_60Hz_16x9                 9
 #define VIC_2880x480i_60Hz_4x3                 10
 #define VIC_2880x480i_60Hz_16x9                11
 #define VIC_2880x240p_60Hz_4x3                 12
 #define VIC_2880x240p_60Hz_16x9                13
 #define VIC_1440x480p_60Hz_4x3                 14
 #define VIC_1440x480p_60Hz_16x9                15
 #define VIC_1920x1080p_60Hz_16x9               16
 #define VIC_720x576p_50Hz_4x3                  17
 #define VIC_720x576p_50Hz_16x9                 18
 #define VIC_1280x720p_50Hz_16x9                19
 #define VIC_1920x1080i_50Hz_16x9               20
 #define VIC_720x576i_50Hz_4x3                  21
 #define VIC_720x576i_50Hz_16x9                 22
 #define VIC_720x288p_50Hz_4x3                  23
 #define VIC_720x288p_50Hz_16x9                 24
 #define VIC_2880x576i_50Hz_4x3                 25
 #define VIC_2880x576i_50Hz_16x9                26
 #define VIC_2880x288p_50Hz_4x3                 27
 #define VIC_2880x288p_50Hz_16x9                28
 #define VIC_1440x576p_50Hz_4x3                 29
 #define VIC_1440x576p_50Hz_16x9                30
 #define VIC_1920x1080p_50Hz_16x9               31
 #define VIC_1920x1080p_24Hz_16x9               32
 #define VIC_1920x1080p_25Hz_16x9               33
 #define VIC_1920x1080p_30Hz_16x9               34
 #define VIC_2880x480p_60Hz_4x3                 35
 #define VIC_2880x480p_60Hz_16x9                36
 #define VIC_2880x576p_50Hz_4x3                 37
 #define VIC_2880x576p_50Hz_16x9                38
 #define VIC_1920x1080i_1250_50Hz_16x9          39
 #define VIC_1920x1080i_100Hz_16x9              40
 #define VIC_1280x720p_100Hz_16x9               41
 #define VIC_720x576p_100Hz_4x3                 42
 #define VIC_720x576p_100Hz_16x9                43
 #define VIC_720x576i_100Hz_4x3                 44
 #define VIC_720x576i_100Hz_16x9                45
 #define VIC_1920x1080i_120Hz_16x9              46
 #define VIC_1280x720p_120Hz_16x9               47
 #define VIC_720x480p_120Hz_4x3                 48
 #define VIC_720x480p_120Hz_16x9                49
 #define VIC_720x480i_120Hz_4x3                 50
 #define VIC_720x480i_120Hz_16x9                51
 #define VIC_720x576p_200Hz_4x3                 52
 #define VIC_720x576p_200Hz_16x9                53
 #define VIC_720x576i_200Hz_4x3                 54
 #define VIC_720x576i_200Hz_16x9                55
 #define VIC_720x480p_240Hz_4x3                 56
 #define VIC_720x480p_240Hz_16x9                57
 #define VIC_720x480i_240Hz_4x3                 58
 #define VIC_720x480i_240Hz_16x9                59
 #define VIC_1280x720p_24Hz_16x9                60
 #define VIC_1280x720p_25Hz_16x9                61
 #define VIC_1280x720p_30Hz_16x9                62
 #define VIC_1920x1080p_120Hz_16x9              63
 #define VIC_1920x1080p_100Hz_16x9              64
 #define VESA_OFFSET                            300
 #define VIC_VESA_640x480p_60Hz_4x3             300
 #define VIC_VESA_1280x800p_60Hz_16x9           301
 #define VIC_VESA_1280x1024p_60Hz_5x4           302
 #define VIC_VESA_1920x1200p_60Hz_8x5           303
 #define VIC_VESA_800x600p_60Hz                 304
 #define VIC_VESA_1024x768p_60Hz                305

void hdmitx_writereg(vim2_display_t* display, uint32_t addr, uint32_t data);
uint32_t hdmitx_readreg(vim2_display_t* display, uint32_t addr);
zx_status_t init_hdmi_hardware(vim2_display_t* display);
void dump_regs(vim2_display_t* display);
zx_status_t init_hdmi_interface(vim2_display_t* display, const struct hdmi_param* p);
void hdmi_test(vim2_display_t* display, uint32_t width);
zx_status_t configure_pll(vim2_display_t* display, const struct hdmi_param* p,
    const struct pll_param* pll);
void hdmi_shutdown(vim2_display_t* display);