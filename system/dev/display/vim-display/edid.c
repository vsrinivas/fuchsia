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

    if (start_ext[0] != 0x2) {
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

static void get_vic(vim2_display_t* display)
{
    uint32_t i;
    ZX_DEBUG_ASSERT(display);

    disp_timing_t* disp_timing = &display->std_disp_timing;
    ZX_DEBUG_ASSERT(disp_timing);

    struct hdmi_param** supportedFormats = get_supported_formats();
    ZX_DEBUG_ASSERT(supportedFormats);

    for (i = 0; supportedFormats[i] != NULL; i++) {
        if (supportedFormats[i]->timings.hactive != disp_timing->HActive) {
            continue;
        }
        if (supportedFormats[i]->timings.vactive != disp_timing->VActive) {
            continue;
        }
        display->p = supportedFormats[i];
        return;
    }

    DISP_ERROR("Display preferred resolution not supported (%d x %d%c [flag = 0x%x])\n",
        disp_timing->HActive, disp_timing->VActive, disp_timing->Flags & (0x80) ? 'i' : 'p',
        disp_timing->Flags);
    DISP_ERROR("Use default: 640x480p60Hz\n");
    display->p = &hdmi_640x480p60Hz_vft;
    return;
}

static void dump_raw_edid(const uint8_t* edid_buf, uint16_t edid_buf_size)
{
    ZX_DEBUG_ASSERT(edid_buf);

    for (int i = 0; i < edid_buf_size; i++) {
        DISP_ERROR("0x%x\n", edid_buf[i]);
    }
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
    get_vic(display);

    return ZX_OK;

}