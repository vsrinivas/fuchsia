// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "edid.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <hw/reg.h>

bool edid_has_extension(const uint8_t* edid_buf) {
    const edid_t* edid = (edid_t *) edid_buf;

    return (edid->ext_flag == 1);
}

zx_status_t edid_get_num_dtd(const uint8_t* edid_buf, uint8_t* num_dtd) {
    const uint8_t* start_ext;
    const uint8_t* start_dtd;
    *num_dtd = 0;
    int i;

    if (!edid_has_extension(edid_buf)) {
        *num_dtd = 0;
        return ZX_OK;
    }

    // It has extension. Read from start of DTD until you hit 00 00
    start_ext = &edid_buf[128];

    if (start_ext[0] != 0x2) {
        zxlogf(ERROR, "%s: Unknown tag! %d\n", __FUNCTION__, start_ext[0]);
        return ZX_ERR_WRONG_TYPE;
    }

    if (start_ext[2] == 0) {
        zxlogf(ERROR, "%s: Invalid DTD pointer! 0x%x\n", __FUNCTION__, start_ext[2]);
        return ZX_ERR_WRONG_TYPE;
    }

    start_dtd = &start_ext[0] + start_ext[2];
    i = 0;
    while (start_dtd[i] != 0x0 && start_dtd[i + 1] != 0x0) {
        *num_dtd += 1;
        i += 18;
    }

    return ZX_OK;
}

void edid_dump_disp_timing(const disp_timing_t* d) {
    zxlogf(INFO, "%s\n", __FUNCTION__);

    zxlogf(INFO, "pixel_clk = 0x%x\n", d->pixel_clk);
    zxlogf(INFO, "HActive = 0x%x\n", d->HActive);
    zxlogf(INFO, "HBlanking = 0x%x\n", d->HBlanking);
    zxlogf(INFO, "VActive = 0x%x\n", d->VActive);
    zxlogf(INFO, "VBlanking = 0x%x\n", d->VBlanking);
    zxlogf(INFO, "HSyncOffset = 0x%x\n", d->HSyncOffset);
    zxlogf(INFO, "HSyncPulseWidth = 0x%x\n", d->HSyncPulseWidth);
    zxlogf(INFO, "VSyncOffset = 0x%x\n", d->VSyncOffset);
    zxlogf(INFO, "VSyncPulseWidth = 0x%x\n", d->VSyncPulseWidth);
    zxlogf(INFO, "HImageSize = 0x%x\n", d->HImageSize);
    zxlogf(INFO, "VImageSize = 0x%x\n", d->VImageSize);
    zxlogf(INFO, "HBorder = 0x%x\n", d->HBorder);
    zxlogf(INFO, "VBorder = 0x%x\n", d->VBorder);
    zxlogf(INFO, "Flags = 0x%x\n", d->Flags);
}

zx_status_t edid_parse_std_display_timing(const uint8_t* edid_buf, detailed_timing_t* raw_dtd,
                                            disp_timing_t* disp_timing) {
    const uint8_t* start_dtd;
    uint8_t* s_r_dtd = (uint8_t*) raw_dtd;

    start_dtd = &edid_buf[0x36];

    // populate raw structure first
    memcpy(s_r_dtd, start_dtd, 18);

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
    return ZX_OK;
}

zx_status_t edid_parse_display_timing(const uint8_t* edid_buf, detailed_timing_t* raw_dtd,
                                                    disp_timing_t* disp_timing, uint8_t num_dtd) {
    const uint8_t* start_ext;
    const uint8_t* start_dtd;
    uint8_t* s_r_dtd = (uint8_t*) raw_dtd;

    if (!edid_has_extension(edid_buf)) {
        return ZX_ERR_INVALID_ARGS;
    }

    // It has extension. Read from start of DTD until you hit 00 00
    start_ext = &edid_buf[128];

    if (start_ext[0] != 0x2) {
        zxlogf(ERROR, "%s: Unknown tag! %d\n", __FUNCTION__, start_ext[0]);
        return ZX_ERR_WRONG_TYPE;
    }

    if (start_ext[2] == 0) {
        zxlogf(ERROR, "%s: Invalid DTD pointer! 0x%x\n", __FUNCTION__, start_ext[2]);
        return ZX_ERR_WRONG_TYPE;
    }

    start_dtd = &start_ext[0] + start_ext[2];


    for (int i = 0; i < num_dtd; i++) {
        // populate raw structure first
        memcpy(s_r_dtd, start_dtd, 18);

        disp_timing[i].pixel_clk = raw_dtd[i].raw_pixel_clk[1] << 8 |
                                                        raw_dtd[i].raw_pixel_clk[0];
        disp_timing[i].HActive = (((raw_dtd[i].raw_Hact_HBlank & 0xf0)>>4) << 8) |
                                                        raw_dtd[i].raw_Hact;
        disp_timing[i].HBlanking = ((raw_dtd[i].raw_Hact_HBlank & 0x0f) << 8) |
                                                        raw_dtd[i].raw_HBlank;
        disp_timing[i].VActive = (((raw_dtd[i].raw_Vact_VBlank & 0xf0)>>4) << 8) |
                                                        raw_dtd[i].raw_Vact;
        disp_timing[i].VBlanking = ((raw_dtd[i].raw_Vact_VBlank & 0x0f) << 8) |
                                                        raw_dtd[i].raw_VBlank;
        disp_timing[i].HSyncOffset = (((raw_dtd[i].raw_HSync_VSync_OFF_PW & 0xc0)>>6) << 8) |
                                                        raw_dtd[i].raw_HSyncOff;
        disp_timing[i].HSyncPulseWidth = (((raw_dtd[i].raw_HSync_VSync_OFF_PW & 0x30)>>4) << 8) |
                                                        raw_dtd[i].raw_HSyncPW;
        disp_timing[i].VSyncOffset = (((raw_dtd[i].raw_HSync_VSync_OFF_PW & 0x0c)>>2) << 4) |
                                                        (raw_dtd[i].raw_VSyncOff_VSyncPW & 0xf0)>>4;
        disp_timing[i].VSyncPulseWidth = ((raw_dtd[i].raw_HSync_VSync_OFF_PW & 0x03) << 4) |
                                                        (raw_dtd[i].raw_VSyncOff_VSyncPW & 0x0f);
        disp_timing[i].HImageSize = (((raw_dtd[i].raw_H_V_ImageSize & 0xf0)>>4)<<8) |
                                                        raw_dtd[i].raw_HImageSize;
        disp_timing[i].VImageSize = ((raw_dtd[i].raw_H_V_ImageSize & 0x0f)<<8) |
                                                        raw_dtd[i].raw_VImageSize;
        disp_timing[i].HBorder = raw_dtd[i].raw_HBorder;
        disp_timing[i].VBorder = raw_dtd[i].raw_VBorder;
        disp_timing[i].Flags = raw_dtd[i].raw_Flags;
        s_r_dtd += 18;
        start_dtd += 18;
    }
    return ZX_OK;
}