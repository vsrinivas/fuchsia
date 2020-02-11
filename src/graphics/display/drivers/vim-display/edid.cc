// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "edid.h"

#include <ddk/debug.h>

#include "vim-display.h"

zx_status_t get_vic(const display_mode_t* disp_timing, struct hdmi_param* p) {
  // Monitor has its own preferred timings. Use that
  p->timings.interlace_mode = disp_timing->flags & MODE_FLAG_INTERLACED;
  p->timings.pfreq = (disp_timing->pixel_clock_10khz * 10);  // KHz
  // TODO: pixel repetition is 0 for most progressive. We don't support interlaced
  p->timings.pixel_repeat = 0;
  p->timings.hactive = disp_timing->h_addressable;
  p->timings.hblank = disp_timing->h_blanking;
  p->timings.hfront = disp_timing->h_front_porch;
  p->timings.hsync = disp_timing->h_sync_pulse;
  p->timings.htotal = (p->timings.hactive) + (p->timings.hblank);
  p->timings.hback = (p->timings.hblank) - (p->timings.hfront + p->timings.hsync);
  p->timings.hpol = disp_timing->flags & MODE_FLAG_HSYNC_POSITIVE;

  p->timings.vactive = disp_timing->v_addressable;
  p->timings.vblank0 = disp_timing->v_blanking;
  p->timings.vfront = disp_timing->v_front_porch;
  p->timings.vsync = disp_timing->v_sync_pulse;
  p->timings.vtotal = (p->timings.vactive) + (p->timings.vblank0);
  p->timings.vback = (p->timings.vblank0) - (p->timings.vfront + p->timings.vsync);
  p->timings.vpol = disp_timing->flags & MODE_FLAG_VSYNC_POSITIVE;

  // FIXE: VENC Repeat is undocumented. It seems to be only needed for the following
  // resolutions: 1280x720p60, 1280x720p50, 720x480p60, 720x480i60, 720x576p50, 720x576i50
  // For now, we will simply not support this feature.
  p->timings.venc_pixel_repeat = 0;
  // Let's make sure we support what we've got so far
  if (p->timings.interlace_mode) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (p->timings.vactive == 2160) {
    DISP_INFO("4K Monitor Detected.\n");

    if (p->timings.pfreq == 533250) {
      // FIXME: 4K with reduced blanking (533.25MHz) does not work
      DISP_INFO("4K @ 30Hz\n");
      p->timings.interlace_mode = 0;
      p->timings.pfreq = (297000);  // KHz
      p->timings.pixel_repeat = 0;
      p->timings.hactive = 3840;
      p->timings.hblank = 560;
      p->timings.hfront = 176;
      p->timings.hsync = 88;
      p->timings.htotal = (p->timings.hactive) + (p->timings.hblank);
      p->timings.hback = (p->timings.hblank) - (p->timings.hfront + p->timings.hsync);
      p->timings.hpol = 1;
      p->timings.vactive = 2160;
      p->timings.vblank0 = 90;
      p->timings.vfront = 8;
      p->timings.vsync = 10;
      p->timings.vtotal = (p->timings.vactive) + (p->timings.vblank0);
      p->timings.vback = (p->timings.vblank0) - (p->timings.vfront + p->timings.vsync);
      p->timings.vpol = 1;
    }
  }

  if (p->timings.pfreq > 500000) {
    p->is4K = true;
  } else {
    p->is4K = false;
  }

  if (p->timings.hactive * 3 == p->timings.vactive * 4) {
    p->aspect_ratio = HDMI_ASPECT_RATIO_4x3;
  } else if (p->timings.hactive * 9 == p->timings.vactive * 16) {
    p->aspect_ratio = HDMI_ASPECT_RATIO_16x9;
  } else {
    p->aspect_ratio = HDMI_ASPECT_RATIO_NONE;
  }

  p->colorimetry = HDMI_COLORIMETRY_ITU601;

  if (p->timings.pfreq > 500000) {
    p->phy_mode = 1;
  } else if (p->timings.pfreq > 200000) {
    p->phy_mode = 2;
  } else if (p->timings.pfreq > 100000) {
    p->phy_mode = 3;
  } else {
    p->phy_mode = 4;
  }

  // TODO: We probably need a more sophisticated method for calculating
  // clocks. This will do for now.
  p->pll_p_24b.viu_channel = 1;
  p->pll_p_24b.viu_type = VIU_ENCP;
  p->pll_p_24b.vid_pll_div = VID_PLL_DIV_5;
  p->pll_p_24b.vid_clk_div = 2;
  p->pll_p_24b.hdmi_tx_pixel_div = 1;
  p->pll_p_24b.encp_div = 1;
  p->pll_p_24b.od1 = 1;
  p->pll_p_24b.od2 = 1;
  p->pll_p_24b.od3 = 1;

  p->pll_p_24b.hpll_clk_out = (p->timings.pfreq * 10);
  while (p->pll_p_24b.hpll_clk_out < 2900000) {
    if (p->pll_p_24b.od1 < 4) {
      p->pll_p_24b.od1 *= 2;
      p->pll_p_24b.hpll_clk_out *= 2;
    } else if (p->pll_p_24b.od2 < 4) {
      p->pll_p_24b.od2 *= 2;
      p->pll_p_24b.hpll_clk_out *= 2;
    } else if (p->pll_p_24b.od3 < 4) {
      p->pll_p_24b.od3 *= 2;
      p->pll_p_24b.hpll_clk_out *= 2;
    } else {
      return ZX_ERR_OUT_OF_RANGE;
    }
  }
  if (p->pll_p_24b.hpll_clk_out > 6000000) {
    DISP_ERROR("Something went wrong in clock calculation (pll_out = %d)\n",
               p->pll_p_24b.hpll_clk_out);
    return ZX_ERR_OUT_OF_RANGE;
  }

  return ZX_OK;
}
