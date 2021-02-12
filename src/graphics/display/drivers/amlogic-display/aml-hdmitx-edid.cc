// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>

#include "aml-hdmitx.h"

namespace amlogic_display {

zx_status_t AmlHdmitx::GetVic(const display_mode_t* disp_timing) {
  // Monitor has its own preferred timings. Use that
  p_.timings.interlace_mode = disp_timing->flags & MODE_FLAG_INTERLACED;
  p_.timings.pfreq = (disp_timing->pixel_clock_10khz * 10);  // KHz
  // TODO: pixel repetition is 0 for most progressive. We don't support interlaced
  p_.timings.pixel_repeat = 0;
  p_.timings.hactive = disp_timing->h_addressable;
  p_.timings.hblank = disp_timing->h_blanking;
  p_.timings.hfront = disp_timing->h_front_porch;
  p_.timings.hsync = disp_timing->h_sync_pulse;
  p_.timings.htotal = (p_.timings.hactive) + (p_.timings.hblank);
  p_.timings.hback = (p_.timings.hblank) - (p_.timings.hfront + p_.timings.hsync);
  p_.timings.hpol = disp_timing->flags & MODE_FLAG_HSYNC_POSITIVE;

  p_.timings.vactive = disp_timing->v_addressable;
  p_.timings.vblank0 = disp_timing->v_blanking;
  p_.timings.vfront = disp_timing->v_front_porch;
  p_.timings.vsync = disp_timing->v_sync_pulse;
  p_.timings.vtotal = (p_.timings.vactive) + (p_.timings.vblank0);
  p_.timings.vback = (p_.timings.vblank0) - (p_.timings.vfront + p_.timings.vsync);
  p_.timings.vpol = disp_timing->flags & MODE_FLAG_VSYNC_POSITIVE;

  // FIXE: VENC Repeat is undocumented. It seems to be only needed for the following
  // resolutions: 1280x720p60, 1280x720p50, 720x480p60, 720x480i60, 720x576p50, 720x576i50
  // For now, we will simply not support this feature.
  p_.timings.venc_pixel_repeat = 0;
  // Let's make sure we support what we've got so far
  if (p_.timings.interlace_mode) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (p_.timings.vactive == 2160) {
    DISP_INFO("4K Monitor Detected.\n");

    if (p_.timings.pfreq == 533250) {
      // FIXME: 4K with reduced blanking (533.25MHz) does not work
      DISP_INFO("4K @ 30Hz\n");
      p_.timings.interlace_mode = 0;
      p_.timings.pfreq = (297000);  // KHz
      p_.timings.pixel_repeat = 0;
      p_.timings.hactive = 3840;
      p_.timings.hblank = 560;
      p_.timings.hfront = 176;
      p_.timings.hsync = 88;
      p_.timings.htotal = (p_.timings.hactive) + (p_.timings.hblank);
      p_.timings.hback = (p_.timings.hblank) - (p_.timings.hfront + p_.timings.hsync);
      p_.timings.hpol = 1;
      p_.timings.vactive = 2160;
      p_.timings.vblank0 = 90;
      p_.timings.vfront = 8;
      p_.timings.vsync = 10;
      p_.timings.vtotal = (p_.timings.vactive) + (p_.timings.vblank0);
      p_.timings.vback = (p_.timings.vblank0) - (p_.timings.vfront + p_.timings.vsync);
      p_.timings.vpol = 1;
    }
  }

  if (p_.timings.pfreq > 500000) {
    p_.is4K = true;
  } else {
    p_.is4K = false;
  }

  if (p_.timings.hactive * 3 == p_.timings.vactive * 4) {
    p_.aspect_ratio = HDMI_ASPECT_RATIO_4x3;
  } else if (p_.timings.hactive * 9 == p_.timings.vactive * 16) {
    p_.aspect_ratio = HDMI_ASPECT_RATIO_16x9;
  } else {
    p_.aspect_ratio = HDMI_ASPECT_RATIO_NONE;
  }

  p_.colorimetry = HDMI_COLORIMETRY_ITU601;

  if (p_.timings.pfreq > 500000) {
    p_.phy_mode = 1;
  } else if (p_.timings.pfreq > 200000) {
    p_.phy_mode = 2;
  } else if (p_.timings.pfreq > 100000) {
    p_.phy_mode = 3;
  } else {
    p_.phy_mode = 4;
  }

  // TODO: We probably need a more sophisticated method for calculating
  // clocks. This will do for now.
  p_.pll_p_24b.viu_channel = 1;
  p_.pll_p_24b.viu_type = VIU_ENCP;
  p_.pll_p_24b.vid_pll_div = VID_PLL_DIV_5;
  p_.pll_p_24b.vid_clk_div = 2;
  p_.pll_p_24b.hdmi_tx_pixel_div = 1;
  p_.pll_p_24b.encp_div = 1;
  p_.pll_p_24b.od1 = 1;
  p_.pll_p_24b.od2 = 1;
  p_.pll_p_24b.od3 = 1;

  p_.pll_p_24b.hpll_clk_out = (p_.timings.pfreq * 10);
  while (p_.pll_p_24b.hpll_clk_out < 2900000) {
    if (p_.pll_p_24b.od1 < 4) {
      p_.pll_p_24b.od1 *= 2;
      p_.pll_p_24b.hpll_clk_out *= 2;
    } else if (p_.pll_p_24b.od2 < 4) {
      p_.pll_p_24b.od2 *= 2;
      p_.pll_p_24b.hpll_clk_out *= 2;
    } else if (p_.pll_p_24b.od3 < 4) {
      p_.pll_p_24b.od3 *= 2;
      p_.pll_p_24b.hpll_clk_out *= 2;
    } else {
      return ZX_ERR_OUT_OF_RANGE;
    }
  }
  if (p_.pll_p_24b.hpll_clk_out > 6000000) {
    DISP_ERROR("Something went wrong in clock calculation (pll_out = %d)\n",
               p_.pll_p_24b.hpll_clk_out);
    return ZX_ERR_OUT_OF_RANGE;
  }

  return ZX_OK;
}

}  // namespace amlogic_display
