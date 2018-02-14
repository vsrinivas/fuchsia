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

struct hdmi_param* supportedFormats[] = {
    &hdmi_640x480p60Hz_vft,
    &hdmi_720x480p60Hz_vft,
    &hdmi_1280x720p60Hz_vft,
    &hdmi_1280x800p60Hz_vft,
    &hdmi_1280x1024p60Hz_vft,
    &hdmi_1920x1080p60Hz_vft,
    NULL,
};

struct hdmi_param** get_supported_formats(void)
{
    return supportedFormats;
}

struct hdmi_param hdmi_640x480p60Hz_vft = {
    .vic = VIC_VESA_640x480p_60Hz_4x3,
    .enc_lut = ENC_LUT_640x480p,
    .aspect_ratio = HDMI_ASPECT_RATIO_4x3,
    .colorimety = HDMI_COLORIMETY_ITU601, // if ref std is smte[x], set to 709, else 601
    .timings = {
        .interlace_mode =       0,          // progressive
        .pfreq =                25175,     // KHz
        .ln =                   1,
        .pixel_repeat =         0,
        .venc_pixel_repeat =    0,          // TODO: ?
        .hfreq =                26218,      // Hz
        .hactive =              640,
        .htotal =               800,
        .hblank =               160,
        .hfront =               16,
        .hsync =                96,
        .hback =                48,
        .hpol =                 0,          // 0: N, 1: P,
        .vfreq =                59940,
        .vactive =              480,
        .vtotal =               525,
        .vblank0 =              45,
        .vblank1 =              0,         // ignore for progressive mode
        .vfront =               10,
        .vsync =                2,
        .vback =                33,
        .vpol =                 0,
    },
    .phy_mode = 4,
    .pll_p_24b = {
        .viu_channel =          1,
        .viu_type =             VIU_ENCP,
        .hpll_clk_out =         4028000,
        .od1 =                  4,
        .od2 =                  4,
        .od3 =                  1,
        .vid_pll_div =          VID_PLL_DIV_5,
        .vid_clk_div =          2,
        .hdmi_tx_pixel_div =    1,
        .encp_div =             1,
        .enci_div =             -1,
    },
    .pll_p_30b = {
        .viu_channel =          -1,
        .viu_type =             -1,
        .hpll_clk_out =         -1,
        .od1 =                  -1,
        .od2 =                  -1,
        .od3 =                  -1,
        .vid_pll_div =          -1,
        .vid_clk_div =          -1,
        .hdmi_tx_pixel_div =    -1,
        .encp_div =             -1,
        .enci_div =             -1,
    },
    .pll_p_36b = {
        .viu_channel =          -1,
        .viu_type =             -1,
        .hpll_clk_out =         -1,
        .od1 =                  -1,
        .od2 =                  -1,
        .od3 =                  -1,
        .vid_pll_div =          -1,
        .vid_clk_div =          -1,
        .hdmi_tx_pixel_div =    -1,
        .encp_div =             -1,
        .enci_div =             -1,
    },
};

struct hdmi_param hdmi_720x480p60Hz_vft = {
    .vic = VIC_720x480p_60Hz_16x9,
    .enc_lut = ENC_LUT_480p,
    .aspect_ratio = HDMI_ASPECT_RATIO_16x9,
    .colorimety = HDMI_COLORIMETY_ITU601, // if ref std is smte[x], set to 709, else 601
    .timings = {
        .interlace_mode =       0,          // progressive
        .pfreq =                27000,     // KHz
        .ln =                   7,
        .pixel_repeat =         0,
        .venc_pixel_repeat =    1,          // TODO: ?
        .hfreq =                31469,      // Hz
        .hactive =              720,
        .htotal =               858,
        .hblank =               138,
        .hfront =               16,
        .hsync =                62,
        .hback =                60,
        .hpol =                 0,          // 0: N, 1: P,
        .vfreq =                59940,
        .vactive =              480,
        .vtotal =               525,
        .vblank0 =              45,
        .vblank1 =              0,         // ignore for progressive mode
        .vfront =               9,
        .vsync =                6,
        .vback =                30,
        .vpol =                 0,
    },
    .phy_mode = 4,
    .pll_p_24b = {
        .viu_channel =          1,
        .viu_type =             VIU_ENCP,
        .hpll_clk_out =         4324320,
        .od1 =                  4,
        .od2 =                  4,
        .od3 =                  1,
        .vid_pll_div =          VID_PLL_DIV_5,
        .vid_clk_div =          1,
        .hdmi_tx_pixel_div =    2,
        .encp_div =             1,
        .enci_div =             -1,
    },
    .pll_p_30b = {
        .viu_channel =          1,
        .viu_type =             VIU_ENCP,
        .hpll_clk_out =         5405400,
        .od1 =                  4,
        .od2 =                  4,
        .od3 =                  1,
        .vid_pll_div =          VID_PLL_DIV_6p25,
        .vid_clk_div =          1,
        .hdmi_tx_pixel_div =    2,
        .encp_div =             1,
        .enci_div =             -1,
    },
    .pll_p_36b = {
        .viu_channel =          1,
        .viu_type =             VIU_ENCP,
        .hpll_clk_out =         3243240,
        .od1 =                  2,
        .od2 =                  4,
        .od3 =                  1,
        .vid_pll_div =          VID_PLL_DIV_7p5,
        .vid_clk_div =          1,
        .hdmi_tx_pixel_div =    2,
        .encp_div =             1,
        .enci_div =             -1,
    },
};

struct hdmi_param hdmi_1280x720p60Hz_vft = {
    .vic = VIC_1280x720p_60Hz_16x9,
    .enc_lut = ENC_LUT_720p,
    .aspect_ratio = HDMI_ASPECT_RATIO_16x9,
    .colorimety = HDMI_COLORIMETY_ITU709, // if ref std is smte[x], set to 709, else 601
    .timings = {
        .interlace_mode =       0,          // progressive
        .pfreq =                74250,     // KHz
        .ln =                   1,
        .pixel_repeat =         0,
        .venc_pixel_repeat =    1,          // TODO: ?
        .hfreq =                45000,      // Hz
        .hactive =              1280,
        .htotal =               1650,
        .hblank =               370,
        .hfront =               110,
        .hsync =                40,
        .hback =                220,
        .hpol =                 1,          // 0: N, 1: P,
        .vfreq =                60000,
        .vactive =              720,
        .vtotal =               750,
        .vblank0 =              30,
        .vblank1 =              0,         // ignore for progressive mode
        .vfront =               5,
        .vsync =                5,
        .vback =                20,
        .vpol =                 1,
    },
    .phy_mode = 4,
    .pll_p_24b = {
        .viu_channel =          1,
        .viu_type =             VIU_ENCP,
        .hpll_clk_out =         2970000,
        .od1 =                  4,
        .od2 =                  1,
        .od3 =                  1,
        .vid_pll_div =          VID_PLL_DIV_5,
        .vid_clk_div =          1,
        .hdmi_tx_pixel_div =    2,
        .encp_div =             1,
        .enci_div =             -1,
    },
    .pll_p_30b = {
        .viu_channel =          1,
        .viu_type =             VIU_ENCP,
        .hpll_clk_out =         3712500,
        .od1 =                  4,
        .od2 =                  1,
        .od3 =                  1,
        .vid_pll_div =          VID_PLL_DIV_6p25,
        .vid_clk_div =          1,
        .hdmi_tx_pixel_div =    2,
        .encp_div =             1,
        .enci_div =             -1,
    },
    .pll_p_36b = {
        .viu_channel =          1,
        .viu_type =             VIU_ENCP,
        .hpll_clk_out =         4455000,
        .od1 =                  1,
        .od2 =                  2,
        .od3 =                  2,
        .vid_pll_div =          VID_PLL_DIV_7p5,
        .vid_clk_div =          1,
        .hdmi_tx_pixel_div =    1,
        .encp_div =             1,
        .enci_div =             -1,
    },
};

struct hdmi_param hdmi_1280x800p60Hz_vft = {
    .vic = VIC_VESA_1280x800p_60Hz_16x9,
    .enc_lut = ENC_LUT_800p,
    .aspect_ratio = HDMI_ASPECT_RATIO_16x9,
    .colorimety = HDMI_COLORIMETY_ITU709, // if ref std is smte[x], set to 709, else 601
    .timings = {
        .interlace_mode =       0,          // progressive
        .pfreq =                83500,     // KHz
        .ln =                   1,
        .pixel_repeat =         0,
        .venc_pixel_repeat =    0,          // TODO: ?
        .hfreq =                49380,      // Hz
        .hactive =              1280,
        .htotal =               1440,
        .hblank =               160,
        .hfront =               48,
        .hsync =                32,
        .hback =                80,
        .hpol =                 1,          // 0: N, 1: P,
        .vfreq =                59910,
        .vactive =              800,
        .vtotal =               823,
        .vblank0 =              23,
        .vblank1 =              0,         // ignore for progressive mode
        .vfront =               3,
        .vsync =                6,
        .vback =                14,
        .vpol =                 1,
    },
    .phy_mode = 4,
    .pll_p_24b = {
        .viu_channel =          1,
        .viu_type =             VIU_ENCP,
        .hpll_clk_out =         5680000,
        .od1 =                  4,
        .od2 =                  2,
        .od3 =                  1,
        .vid_pll_div =          VID_PLL_DIV_5,
        .vid_clk_div =          2,
        .hdmi_tx_pixel_div =    1,
        .encp_div =             1,
        .enci_div =             -1,
    },
    .pll_p_30b = {
        .viu_channel =          -1,
        .viu_type =             -1,
        .hpll_clk_out =         -1,
        .od1 =                  -1,
        .od2 =                  -1,
        .od3 =                  -1,
        .vid_pll_div =          -1,
        .vid_clk_div =          -1,
        .hdmi_tx_pixel_div =    -1,
        .encp_div =             -1,
        .enci_div =             -1,
    },
    .pll_p_36b = {
        .viu_channel =          -1,
        .viu_type =             -1,
        .hpll_clk_out =         -1,
        .od1 =                  -1,
        .od2 =                  -1,
        .od3 =                  -1,
        .vid_pll_div =          -1,
        .vid_clk_div =          -1,
        .hdmi_tx_pixel_div =    -1,
        .encp_div =             -1,
        .enci_div =             -1,
    },
};

struct hdmi_param hdmi_1280x1024p60Hz_vft = {
    .vic = VIC_VESA_1280x1024p_60Hz_5x4,
    .enc_lut = ENC_LUT_1280x1024p60hz,
    .aspect_ratio = HDMI_ASPECT_RATIO_16x9,
    .colorimety = HDMI_COLORIMETY_ITU709, // if ref std is smte[x], set to 709, else 601
    .timings = {
        .interlace_mode =       0,          // progressive
        .pfreq =                108000,     // KHz
        .ln =                   1,
        .pixel_repeat =         0,
        .venc_pixel_repeat =    0,          // TODO: ?
        .hfreq =                64080,      // Hz
        .hactive =              1280,
        .htotal =               1688,
        .hblank =               408,
        .hfront =               48,
        .hsync =                112,
        .hback =                248,
        .hpol =                 1,          // 0: N, 1: P,
        .vfreq =                60020,
        .vactive =              1024,
        .vtotal =               1066,
        .vblank0 =              42,
        .vblank1 =              0,         // ignore for progressive mode
        .vfront =               1,
        .vsync =                3,
        .vback =                38,
        .vpol =                 1,
    },
    .phy_mode = 4,
    .pll_p_24b = {
        .viu_channel =          1,
        .viu_type =             VIU_ENCP,
        .hpll_clk_out =         4320000,
        .od1 =                  4,
        .od2 =                  1,
        .od3 =                  1,
        .vid_pll_div =          VID_PLL_DIV_5,
        .vid_clk_div =          2,
        .hdmi_tx_pixel_div =    1,
        .encp_div =             1,
        .enci_div =             -1,
    },
    .pll_p_30b = {
        .viu_channel =          -1,
        .viu_type =             -1,
        .hpll_clk_out =         -1,
        .od1 =                  -1,
        .od2 =                  -1,
        .od3 =                  -1,
        .vid_pll_div =          -1,
        .vid_clk_div =          -1,
        .hdmi_tx_pixel_div =    -1,
        .encp_div =             -1,
        .enci_div =             -1,
    },
    .pll_p_36b = {
        .viu_channel =          -1,
        .viu_type =             -1,
        .hpll_clk_out =         -1,
        .od1 =                  -1,
        .od2 =                  -1,
        .od3 =                  -1,
        .vid_pll_div =          -1,
        .vid_clk_div =          -1,
        .hdmi_tx_pixel_div =    -1,
        .encp_div =             -1,
        .enci_div =             -1,
    },
};

struct hdmi_param hdmi_1920x1080p60Hz_vft = {
    .vic = VIC_1920x1080p_60Hz_16x9,
    .enc_lut = ENC_LUT_1080p,
    .aspect_ratio = HDMI_ASPECT_RATIO_16x9,
    .colorimety = HDMI_COLORIMETY_ITU709,
    .timings = {
        .interlace_mode =       0,          // progressive
        .pfreq =                148500,     // KHz
        .ln =                   1,
        .pixel_repeat =         0,
        .venc_pixel_repeat =    0,          // TODO: ?
        .hfreq =                67500,      // Hz
        .hactive =              1920,
        .htotal =               2200,
        .hblank =               280,
        .hfront =               88,
        .hsync =                44,
        .hback =                148,
        .hpol =                 1,          // 0: N, 1: P,
        .vfreq =                60000,
        .vactive =              1080,
        .vtotal =               1125,
        .vblank0 =              45,
        .vblank1 =              0,         // ignore for progressive mode
        .vfront =               4,
        .vsync =                5,
        .vback =                36,
        .vpol =                 1,
    },
    .phy_mode = 3,
    .pll_p_24b = {
        .viu_channel =          1,
        .viu_type =             VIU_ENCP,
        .hpll_clk_out =         2970000,
        .od1 =                  1,
        .od2 =                  2,
        .od3 =                  2,
        .vid_pll_div =          VID_PLL_DIV_5,
        .vid_clk_div =          1,
        .hdmi_tx_pixel_div =    1,
        .encp_div =             1,
        .enci_div =             -1,
    },
    .pll_p_30b = {
        .viu_channel =          1,
        .viu_type =             VIU_ENCP,
        .hpll_clk_out =         3712500,
        .od1 =                  1,
        .od2 =                  2,
        .od3 =                  2,
        .vid_pll_div =          VID_PLL_DIV_6p25,
        .vid_clk_div =          1,
        .hdmi_tx_pixel_div =    1,
        .encp_div =             1,
        .enci_div =             -1,
    },
    .pll_p_36b = {
        .viu_channel =          1,
        .viu_type =             VIU_ENCP,
        .hpll_clk_out =         4455000,
        .od1 =                  1,
        .od2 =                  2,
        .od3 =                  2,
        .vid_pll_div =          VID_PLL_DIV_7p5,
        .vid_clk_div =          1,
        .hdmi_tx_pixel_div =    1,
        .encp_div =             1,
        .enci_div =             -1,
    },
};

