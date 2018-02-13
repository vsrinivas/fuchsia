// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-device.h>
#include <zircon/listnode.h>
#include <zircon/types.h>
#include <threads.h>

typedef struct {
    uint16_t pixel_clk;
    uint16_t HActive;
    uint16_t HBlanking;
    uint16_t VActive;
    uint16_t VBlanking;
    uint16_t HSyncOffset;
    uint16_t HSyncPulseWidth;
    uint8_t VSyncOffset;
    uint8_t VSyncPulseWidth;
    uint16_t HImageSize;
    uint16_t VImageSize;
    uint8_t HBorder;
    uint8_t VBorder;
    uint8_t Flags;
    uint8_t align[9];
} __attribute__((__packed__)) disp_timing_t;

typedef struct {
    uint8_t raw_pixel_clk[2];                       /* LSB first */
    uint8_t raw_Hact;
    uint8_t raw_HBlank;
    uint8_t raw_Hact_HBlank;
    uint8_t raw_Vact;
    uint8_t raw_VBlank;
    uint8_t raw_Vact_VBlank;
    uint8_t raw_HSyncOff;
    uint8_t raw_HSyncPW;
    uint8_t raw_VSyncOff_VSyncPW;
    uint8_t raw_HSync_VSync_OFF_PW;
    uint8_t raw_HImageSize;
    uint8_t raw_VImageSize;
    uint8_t raw_H_V_ImageSize;
    uint8_t raw_HBorder;
    uint8_t raw_VBorder;
    uint8_t raw_Flags;
} __attribute__((__packed__)) detailed_timing_t;

typedef struct {
    uint8_t header[8];                          /* Header */
    uint8_t id_mfg[2];                          /* ID Manfucturer Name */
    uint8_t id_pcode[2];                        /* ID Produce Code */
    uint8_t id_serial[4];                       /* ID Serial Number */
    uint8_t wom;                                /* Week of Manufacture */
    uint8_t yom;                                /* Year of Manufacture */
    uint8_t version;                            /* Version #*/
    uint8_t revision;                           /* Revision */
    uint8_t vid_input_def;                      /* Video Input Definition */
    uint8_t max_hoz_img_size;                   /* cm */
    uint8_t max_ver_img_size;                   /* cm */
    uint8_t gamma;                              /* Display tranfser characteristics */
    uint8_t feature_support;
    uint8_t color_char[10];                     /* Color Characteristics */
    uint8_t established_timings1;
    uint8_t established_timings2;
    uint8_t mfg_reserved_timings;
    uint8_t std_timing_id[16];
    detailed_timing_t detailed_timing_desc[4];  /* 4 x 18B*/
    uint8_t ext_flag;
    uint8_t cksum;
} __attribute__((__packed__)) edid_t;

bool edid_has_extension(const uint8_t* edid_buf);
zx_status_t edid_get_num_dtd(const uint8_t* edid_buf, uint8_t* num_dtd);
zx_status_t edid_parse_std_display_timing(const uint8_t* edid_buf, detailed_timing_t* raw_dtd,
                                        disp_timing_t* disp_timing);
zx_status_t edid_parse_display_timing(const uint8_t* edid_buf, detailed_timing_t* raw_dtd,
                                            disp_timing_t* std_disp_timing,
                                            disp_timing_t* pref_disp_timing);