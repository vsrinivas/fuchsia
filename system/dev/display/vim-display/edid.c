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
#include "edid.h"


bool edid_has_extension(const uint8_t* edid_buf)
{
    const edid_t* edid = (edid_t *) edid_buf;

    return (edid->ext_flag == 1);
}

bool edid_rgb_disp(const uint8_t* edid_buf)
{
    const edid_t* edid = (edid_t *) edid_buf;
    return (!!((edid->feature_support & (1 << 2)) >> 2));
}

void edid_get_max_size(const uint8_t* edid_buf, uint8_t* hoz, uint8_t* ver)
{
    const edid_t* edid = (edid_t *) edid_buf;
    *hoz = edid->max_hoz_img_size;
    *ver = edid->max_ver_img_size;
}

static char* get_mfg_id(const uint8_t* edid_buf)
{
    char *mfg_str = calloc(1, sizeof(char));;
    mfg_str[0] = ((edid_buf[8] & 0x7c) >> 2) + 'A' - 1;
    mfg_str[1] = (((edid_buf[8] & 0x03) << 3) | (edid_buf[9] & 0xe0) >> 5) + 'A' - 1;
    mfg_str[2] = ((edid_buf[9] & 0x1f)) + 'A' - 1;
    mfg_str[3] = '\0';
    return mfg_str;
}

static uint16_t get_prod_id(const uint8_t* edid_buf)
{
    return ((edid_buf[11] << 8) | (edid_buf[10]));
}

static void edid_dump_disp_timing(const disp_timing_t* d)
{
    DISP_INFO("pixel_clk = 0x%x (%d)\n", d->pixel_clk, d->pixel_clk);
    DISP_INFO("HActive = 0x%x (%d)\n", d->HActive, d->HActive);
    DISP_INFO("HBlanking = 0x%x (%d)\n", d->HBlanking, d->HBlanking);
    DISP_INFO("VActive = 0x%x (%d)\n", d->VActive, d->VActive);
    DISP_INFO("VBlanking = 0x%x (%d)\n", d->VBlanking, d->VBlanking);
    DISP_INFO("HSyncOffset = 0x%x (%d)\n", d->HSyncOffset, d->HSyncOffset);
    DISP_INFO("HSyncPulseWidth = 0x%x (%d)\n", d->HSyncPulseWidth, d->HSyncPulseWidth);
    DISP_INFO("VSyncOffset = 0x%x (%d)\n", d->VSyncOffset, d->VSyncOffset);
    DISP_INFO("VSyncPulseWidth = 0x%x (%d)\n", d->VSyncPulseWidth, d->VSyncPulseWidth);
    DISP_INFO("HImageSize = 0x%x (%d)\n", d->HImageSize, d->HImageSize);
    DISP_INFO("VImageSize = 0x%x (%d)\n", d->VImageSize, d->VImageSize);
    DISP_INFO("HBorder = 0x%x (%d)\n", d->HBorder, d->HBorder);
    DISP_INFO("VBorder = 0x%x (%d)\n", d->VBorder, d->VBorder);
    DISP_INFO("Flags = 0x%x (%d)\n", d->Flags, d->Flags);
}


static void populate_timings(detailed_timing_t* raw_dtd, disp_timing_t* disp_timing)
{
    disp_timing->pixel_clk = raw_dtd->raw_pixel_clk[1] << 8 |
                                                        raw_dtd->raw_pixel_clk[0];
    disp_timing->HActive = (((raw_dtd->raw_Hact_HBlank & 0xf0)>>4) << 8) |
                                                        raw_dtd->raw_Hact;
    disp_timing->HBlanking = ((raw_dtd->raw_Hact_HBlank & 0x0f) << 8) |
                                                        raw_dtd->raw_HBlank;
    disp_timing->VActive = (((raw_dtd->raw_Vact_VBlank & 0xf0)>>4) << 8) |
                                                        raw_dtd->raw_Vact;
    disp_timing->VBlanking = ((raw_dtd->raw_Vact_VBlank & 0x0f) << 8) |
                                                        raw_dtd->raw_VBlank;
    disp_timing->HSyncOffset = (((raw_dtd->raw_HSync_VSync_OFF_PW & 0xc0)>>6) << 8) |
                                                        raw_dtd->raw_HSyncOff;
    disp_timing->HSyncPulseWidth = (((raw_dtd->raw_HSync_VSync_OFF_PW & 0x30)>>4) << 8) |
                                                        raw_dtd->raw_HSyncPW;
    disp_timing->VSyncOffset = (((raw_dtd->raw_HSync_VSync_OFF_PW & 0x0c)>>2) << 4) |
                                                        (raw_dtd->raw_VSyncOff_VSyncPW & 0xf0)>>4;
    disp_timing->VSyncPulseWidth = ((raw_dtd->raw_HSync_VSync_OFF_PW & 0x03) << 4) |
                                                        (raw_dtd->raw_VSyncOff_VSyncPW & 0x0f);
    disp_timing->HImageSize = (((raw_dtd->raw_H_V_ImageSize & 0xf0)>>4)<<8) |
                                                        raw_dtd->raw_HImageSize;
    disp_timing->VImageSize = ((raw_dtd->raw_H_V_ImageSize & 0x0f)<<8) |
                                                        raw_dtd->raw_VImageSize;
    disp_timing->HBorder = raw_dtd->raw_HBorder;
    disp_timing->VBorder = raw_dtd->raw_VBorder;
    disp_timing->Flags = raw_dtd->raw_Flags;
}


/* This function reads the detailed timing found in block0 and block1 (referred to standard
 * and preferred
 */
zx_status_t edid_parse_display_timing(const uint8_t* edid_buf, detailed_timing_t* raw_dtd,
                                            disp_timing_t* std_disp_timing,
                                            disp_timing_t* pref_disp_timing)
{
    const uint8_t* start_dtd;
    const uint8_t* start_ext;

    uint8_t* s_r_dtd = (uint8_t*) raw_dtd;

    ZX_DEBUG_ASSERT(edid_buf);
    ZX_DEBUG_ASSERT(raw_dtd);
    ZX_DEBUG_ASSERT(std_disp_timing);
    ZX_DEBUG_ASSERT(pref_disp_timing);

    start_dtd = &edid_buf[0x36];

    // populate raw structure first
    memcpy(s_r_dtd, start_dtd, 18);
    populate_timings(raw_dtd, std_disp_timing);

    if (!edid_has_extension(edid_buf)) {
        DISP_INFO("extension flag not found!\n");
        // we should exit here but there are cases where there were more blocks eventhough flag was
        // not set. So we'll read it anyways
        // return ZX_OK;
    }

    // It has extension. Read from start of DTD until you hit 00 00
    start_ext = &edid_buf[128];

    if (start_ext[0] != 0x2 ) {
        if(!edid_has_extension(edid_buf)) {
            // No extension and no valid tag. Not worth reading on.
            return ZX_OK;
        }
        DISP_ERROR("%s: Unknown tag! %d\n", __FUNCTION__, start_ext[0]);
        return ZX_ERR_WRONG_TYPE;
    }

    if (start_ext[2] == 0) {
        DISP_ERROR("%s: Invalid DTD pointer! 0x%x\n", __FUNCTION__, start_ext[2]);
        return ZX_ERR_WRONG_TYPE;
    }

    start_dtd = &start_ext[0] + start_ext[2];

    // populate raw structure first
    memcpy(s_r_dtd, start_dtd, 18);
    populate_timings(&raw_dtd[0], pref_disp_timing);

    return ZX_OK;
}

static uint8_t getInterlaced(uint8_t flag)
{
    return ((flag & (0x80)) >> 7); // TODO: bit defs)
}

static uint8_t getHPol(uint8_t flag)
{
    if(((flag & 0x18) >> 3) == 0x3) {
        // sync type is 3 (i.e. digitail separate)
        return ((flag & (0x02)) >> 1);
    }
    return 0;
}

static uint8_t getVPol(uint8_t flag)
{
    if(((flag & 0x18) >> 3) == 0x3) {
        // sync type is 3 (i.e. digitail separate)
        return ((flag & (0x04)) >> 2);
    }
    return 0;
}

static zx_status_t get_vic(vim2_display_t* display)
{
    disp_timing_t* disp_timing;
    disp_timing= &display->std_disp_timing;
    ZX_DEBUG_ASSERT(disp_timing);

    // Monitor has its own preferred timings. Use that
    display->p->timings.interlace_mode =     getInterlaced(disp_timing->Flags);
    display->p->timings.pfreq =              (disp_timing->pixel_clk * 10); // KHz
    //TODO: pixel repetition is 0 for most progressive. We don't support interlaced
    display->p->timings.pixel_repeat =       0;
    display->p->timings.hactive =            disp_timing->HActive;
    display->p->timings.hblank =             disp_timing->HBlanking;
    display->p->timings.hfront =             disp_timing->HSyncOffset;
    display->p->timings.hsync =              disp_timing->HSyncPulseWidth;
    display->p->timings.htotal =             (display->p->timings.hactive) +
                                                (display->p->timings.hblank);
    display->p->timings.hback =              (display->p->timings.hblank) -
                                                (display->p->timings.hfront +
                                                    display->p->timings.hsync);
    display->p->timings.hpol =               getHPol(disp_timing->Flags);

    display->p->timings.vactive =            disp_timing->VActive;
    display->p->timings.vblank0 =            disp_timing->VBlanking;
    display->p->timings.vfront =             disp_timing->VSyncOffset;
    display->p->timings.vsync =              disp_timing->VSyncPulseWidth;
    display->p->timings.vtotal =             (display->p->timings.vactive) +
                                                (display->p->timings.vblank0);
    display->p->timings.vback =              (display->p->timings.vblank0) -
                                                (display->p->timings.vfront +
                                                    display->p->timings.vsync);
    display->p->timings.vpol =               getVPol(disp_timing->Flags);

    //FIXE: VENC Repeat is undocumented. It seems to be only needed for the following
    // resolutions: 1280x720p60, 1280x720p50, 720x480p60, 720x480i60, 720x576p50, 720x576i50
    // For now, we will simply not support this feature.
    display->p->timings.venc_pixel_repeat = 0;
    // Let's make sure we support what we've got so far
    if (display->p->timings.interlace_mode) {
        DISP_ERROR("ERROR: UNSUPPORTED DISPLAY!!!! Pixel Freq = %d (%s mode)\n",
            display->p->timings.pfreq,
            display->p->timings.interlace_mode? "Interlaced": "Progressive");
        DISP_ERROR("Loading 640x480p as Default\n");

        display->p->timings.interlace_mode =     0;
        display->p->timings.pfreq =              (25175); // KHz
        display->p->timings.pixel_repeat =       0;
        display->p->timings.hactive =            640;
        display->p->timings.hblank =             160;
        display->p->timings.hfront =             16;
        display->p->timings.hsync =              96;
        display->p->timings.htotal =             (display->p->timings.hactive) +
                                                    (display->p->timings.hblank);
        display->p->timings.hback =              (display->p->timings.hblank) -
                                                    (display->p->timings.hfront +
                                                        display->p->timings.hsync);
        display->p->timings.hpol =               1;
        display->p->timings.vactive =            480;
        display->p->timings.vblank0 =            45;
        display->p->timings.vfront =             10;
        display->p->timings.vsync =              2;
        display->p->timings.vtotal =             (display->p->timings.vactive) +
                                                    (display->p->timings.vblank0);
        display->p->timings.vback =              (display->p->timings.vblank0) -
                                                    (display->p->timings.vfront +
                                                        display->p->timings.vsync);
        display->p->timings.vpol =               1;
    }

    if (display->p->timings.vactive == 2160) {
        DISP_INFO("4K Monitor Detected.\n");

        if (display->p->timings.pfreq == 533250) {
            // FIXME: 4K with reduced blanking (533.25MHz) does not work
            DISP_INFO("4K @ 30Hz\n");
            display->p->timings.interlace_mode =     0;
            display->p->timings.pfreq =              (297000); // KHz
            display->p->timings.pixel_repeat =       0;
            display->p->timings.hactive =            3840;
            display->p->timings.hblank =             560;
            display->p->timings.hfront =             176;
            display->p->timings.hsync =              88;
            display->p->timings.htotal =             (display->p->timings.hactive) +
                                                        (display->p->timings.hblank);
            display->p->timings.hback =              (display->p->timings.hblank) -
                                                        (display->p->timings.hfront +
                                                            display->p->timings.hsync);
            display->p->timings.hpol =               1;
            display->p->timings.vactive =            2160;
            display->p->timings.vblank0 =            90;
            display->p->timings.vfront =             8;
            display->p->timings.vsync =              10;
            display->p->timings.vtotal =             (display->p->timings.vactive) +
                                                        (display->p->timings.vblank0);
            display->p->timings.vback =              (display->p->timings.vblank0) -
                                                        (display->p->timings.vfront +
                                                            display->p->timings.vsync);
            display->p->timings.vpol =               1;
        }
    }

    if (display->p->timings.pfreq > 500000) {
        display->p->is4K = true;
    } else {
        display->p->is4K = false;
    }

    // Aspect ratio determination. 4:3 otherwise 16:9
    uint8_t h, v, tmp;
    edid_get_max_size(display->edid_buf, &h, &v);
    if ( (h % 4 == 0) && (v % 3) == 0) {
        tmp = h / 4;
        if ( ((v % tmp) == 0) && ((v / tmp) == 3)) {
                display->p->aspect_ratio = HDMI_ASPECT_RATIO_4x3;
        } else {
            display->p->aspect_ratio = HDMI_ASPECT_RATIO_16x9;
        }
    } else {
        display->p->aspect_ratio = HDMI_ASPECT_RATIO_16x9;
    }

    display->p->colorimetry = HDMI_COLORIMETRY_ITU601;

    if (display->p->timings.pfreq > 500000) {
        display->p->phy_mode = 1;
    } else if (display->p->timings.pfreq > 200000) {
        display->p->phy_mode = 2;
    } else if (display->p->timings.pfreq > 100000) {
        display->p->phy_mode = 3;
    } else {
        display->p->phy_mode = 4;
    }

    //TODO: We probably need a more sophisticated method for calculating
    // clocks. This will do for now.
    display->p->pll_p_24b.viu_channel =          1;
    display->p->pll_p_24b.viu_type =             VIU_ENCP;
    display->p->pll_p_24b.vid_pll_div =          VID_PLL_DIV_5;
    display->p->pll_p_24b.vid_clk_div =          2;
    display->p->pll_p_24b.hdmi_tx_pixel_div =    1;
    display->p->pll_p_24b.encp_div =             1;
    display->p->pll_p_24b.od1 =                  1;
    display->p->pll_p_24b.od2 =                  1;
    display->p->pll_p_24b.od3 =                  1;

    display->p->pll_p_24b.hpll_clk_out = (display->p->timings.pfreq * 10);
    while (display->p->pll_p_24b.hpll_clk_out < 2900000) {
        if (display->p->pll_p_24b.od1 < 4) {
            display->p->pll_p_24b.od1 *= 2;
            display->p->pll_p_24b.hpll_clk_out *= 2;
        } else if (display->p->pll_p_24b.od2 < 4) {
            display->p->pll_p_24b.od2 *= 2;
            display->p->pll_p_24b.hpll_clk_out *= 2;
        } else if (display->p->pll_p_24b.od3 < 4) {
            display->p->pll_p_24b.od3 *= 2;
            display->p->pll_p_24b.hpll_clk_out *= 2;
        } else {
            return ZX_ERR_OUT_OF_RANGE;
        }
    }
    if(display->p->pll_p_24b.hpll_clk_out > 6000000) {
        DISP_ERROR("Something went wrong in clock calculation (pll_out = %d)\n",
            display->p->pll_p_24b.hpll_clk_out);
        return ZX_ERR_OUT_OF_RANGE;
    }

    return ZX_OK;
}

static void dump_raw_edid(const uint8_t* edid_buf, uint16_t edid_buf_size)
{
    ZX_DEBUG_ASSERT(edid_buf);
    zxlogf(INFO, "\n$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n");
    zxlogf(INFO,"$$$$$$$$$$$$ RAW EDID INFO $$$$$$$$$$$$\n");
    zxlogf(INFO, "$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n\n");
    for (int i = 0; i < edid_buf_size; i++) {
        zxlogf(INFO, "0x%02x ", edid_buf[i]);
        if ( ((i + 1) % 8) == 0) {
            zxlogf(INFO, "\n");
        }
    }
    zxlogf(INFO, "$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n\n");
}

zx_status_t get_preferred_res(vim2_display_t* display, uint16_t edid_buf_size)
{
    uint32_t timeout = 0;
    uint32_t addr = 0;
    uint32_t i;
    zx_status_t status;

    ZX_DEBUG_ASSERT(edid_buf_size <= EDID_BUF_SIZE);
    ZX_DEBUG_ASSERT(display);
    ZX_DEBUG_ASSERT(display->edid_buf);
    ZX_DEBUG_ASSERT(display->p);

    memset(display->edid_buf, 0, edid_buf_size);

    for (addr = 0; addr < edid_buf_size; addr+=8) {
        // Program SLAVE/SEGMENT/ADDR
        hdmitx_writereg(display, HDMITX_DWC_I2CM_SLAVE, 0x50);
        hdmitx_writereg(display, HDMITX_DWC_I2CM_SEGADDR, 0x30);
        hdmitx_writereg(display, HDMITX_DWC_I2CM_SEGPTR, 1);
        hdmitx_writereg(display, HDMITX_DWC_I2CM_ADDRESS, addr);
        hdmitx_writereg(display, HDMITX_DWC_I2CM_OPERATION, 1 << 2);

        timeout = 0;
        while ((!(hdmitx_readreg(display, HDMITX_DWC_IH_I2CM_STAT0) & (1 << 1))) && (timeout < 5)) {
            usleep(1000);
            timeout ++;
        }
        if (timeout == 5) {
            DISP_ERROR("HDMI DDC TimeOut\n");
            return ZX_ERR_TIMED_OUT;
        }
        usleep(1000);
        hdmitx_writereg(display, HDMITX_DWC_IH_I2CM_STAT0, 1 << 1);        // clear INT

        for (i = 0; i < 8; i ++) {
            display->edid_buf[addr+i] = hdmitx_readreg(display, HDMITX_DWC_I2CM_READ_BUFF0 + i);
        }
    }

    if ( (status = edid_parse_display_timing(display->edid_buf, &display->std_raw_dtd,
        &display->std_disp_timing, &display->pref_disp_timing)) != ZX_OK) {
            DISP_ERROR("Something went wrong in EDID Parsing (%d)\n", status);
            return status;
    }

    // Find out whether we support the preferred format or not
    status = get_vic(display);

    if (status != ZX_OK) {
        DISP_ERROR("Could not get a proper display timing\n");
        return status;
    }

    // See if we need to change output color to RGB
    if (edid_rgb_disp(display->edid_buf)) {
        display->output_color_format = HDMI_COLOR_FORMAT_RGB;
    } else {
        display->output_color_format = HDMI_COLOR_FORMAT_444;
    }

    dump_raw_edid(display->edid_buf, edid_buf_size);

    return ZX_OK;

}