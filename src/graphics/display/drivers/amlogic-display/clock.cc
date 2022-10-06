// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/amlogic-display/clock.h"

#include <lib/ddk/debug.h>

#include <fbl/alloc_checker.h>

namespace amlogic_display {

namespace {
constexpr uint8_t kMaxPllLockAttempt = 3;
constexpr uint8_t kStv2Sel = 5;
constexpr uint8_t kStv1Sel = 4;
constexpr uint32_t kKHZ = 1000;

void DumpPllCfg(const PllConfig& pll_cfg) {
  DISP_INFO("#############################");
  DISP_INFO("Dumping pll_cfg structure:");
  DISP_INFO("#############################");
  DISP_INFO("fin = 0x%x (%u)", pll_cfg.fin, pll_cfg.fin);
  DISP_INFO("fout = 0x%x (%u)", pll_cfg.fout, pll_cfg.fout);
  DISP_INFO("pll_m = 0x%x (%u)", pll_cfg.pll_m, pll_cfg.pll_m);
  DISP_INFO("pll_n = 0x%x (%u)", pll_cfg.pll_n, pll_cfg.pll_n);
  DISP_INFO("pll_fvco = 0x%x (%u)", pll_cfg.pll_fvco, pll_cfg.pll_fvco);
  DISP_INFO("pll_od1_sel = 0x%x (%u)", pll_cfg.pll_od1_sel, pll_cfg.pll_od1_sel);
  DISP_INFO("pll_od2_sel = 0x%x (%u)", pll_cfg.pll_od2_sel, pll_cfg.pll_od2_sel);
  DISP_INFO("pll_od3_sel = 0x%x (%u)", pll_cfg.pll_od3_sel, pll_cfg.pll_od3_sel);
  DISP_INFO("pll_frac = 0x%x (%u)", pll_cfg.pll_frac, pll_cfg.pll_frac);
  DISP_INFO("pll_fout = 0x%x (%u)", pll_cfg.pll_fout, pll_cfg.pll_fout);
}

void DumpLcdTiming(const LcdTiming& lcd_timing) {
  DISP_INFO("#############################");
  DISP_INFO("Dumping lcd_timing structure:");
  DISP_INFO("#############################");
  DISP_INFO("vid_pixel_on = 0x%x (%u)", lcd_timing.vid_pixel_on, lcd_timing.vid_pixel_on);
  DISP_INFO("vid_line_on = 0x%x (%u)", lcd_timing.vid_line_on, lcd_timing.vid_line_on);
  DISP_INFO("de_hs_addr = 0x%x (%u)", lcd_timing.de_hs_addr, lcd_timing.de_hs_addr);
  DISP_INFO("de_he_addr = 0x%x (%u)", lcd_timing.de_he_addr, lcd_timing.de_he_addr);
  DISP_INFO("de_vs_addr = 0x%x (%u)", lcd_timing.de_vs_addr, lcd_timing.de_vs_addr);
  DISP_INFO("de_ve_addr = 0x%x (%u)", lcd_timing.de_ve_addr, lcd_timing.de_ve_addr);
  DISP_INFO("hs_hs_addr = 0x%x (%u)", lcd_timing.hs_hs_addr, lcd_timing.hs_hs_addr);
  DISP_INFO("hs_he_addr = 0x%x (%u)", lcd_timing.hs_he_addr, lcd_timing.hs_he_addr);
  DISP_INFO("hs_vs_addr = 0x%x (%u)", lcd_timing.hs_vs_addr, lcd_timing.hs_vs_addr);
  DISP_INFO("hs_ve_addr = 0x%x (%u)", lcd_timing.hs_ve_addr, lcd_timing.hs_ve_addr);
  DISP_INFO("vs_hs_addr = 0x%x (%u)", lcd_timing.vs_hs_addr, lcd_timing.vs_hs_addr);
  DISP_INFO("vs_he_addr = 0x%x (%u)", lcd_timing.vs_he_addr, lcd_timing.vs_he_addr);
  DISP_INFO("vs_vs_addr = 0x%x (%u)", lcd_timing.vs_vs_addr, lcd_timing.vs_vs_addr);
  DISP_INFO("vs_ve_addr = 0x%x (%u)", lcd_timing.vs_ve_addr, lcd_timing.vs_ve_addr);
}

void DumpDisplaySettings(const display_setting_t& settings) {
  DISP_INFO("#############################");
  DISP_INFO("Dumping display_setting structure:");
  DISP_INFO("#############################");
  DISP_INFO("lcd_clock = 0x%x (%u)", settings.lcd_clock, settings.lcd_clock);
  DISP_INFO("clock_factor = 0x%x (%u)", settings.clock_factor, settings.clock_factor);
  DISP_INFO("h_period = 0x%x (%u)", settings.h_period, settings.h_period);
  DISP_INFO("h_active = 0x%x (%u)", settings.h_active, settings.h_active);
  DISP_INFO("hsync_bp = 0x%x (%u)", settings.hsync_bp, settings.hsync_bp);
  DISP_INFO("hsync_width = 0x%x (%u)", settings.hsync_width, settings.hsync_width);
  DISP_INFO("v_period = 0x%x (%u)", settings.v_period, settings.v_period);
  DISP_INFO("v_active = 0x%x (%u)", settings.v_active, settings.v_active);
  DISP_INFO("vsync_bp = 0x%x (%u)", settings.vsync_bp, settings.vsync_bp);
  DISP_INFO("vsync_width = 0x%x (%u)", settings.vsync_width, settings.vsync_width);
}

}  // namespace

#define READ32_HHI_REG(a) hhi_mmio_->Read32(a)
#define WRITE32_HHI_REG(a, v) hhi_mmio_->Write32(v, a)

#define READ32_VPU_REG(a) vpu_mmio_->Read32(a)
#define WRITE32_VPU_REG(a, v) vpu_mmio_->Write32(v, a)

// static
LcdTiming Clock::CalculateLcdTiming(const display_setting_t& d) {
  LcdTiming out;
  // Calculate and store DataEnable horizontal and vertical start/stop times
  const uint32_t de_hstart = d.h_period - d.h_active - 1;
  const uint32_t de_vstart = d.v_period - d.v_active;
  out.vid_pixel_on = de_hstart;
  out.vid_line_on = de_vstart;
  out.de_hs_addr = de_hstart;
  out.de_he_addr = de_hstart + d.h_active;
  out.de_vs_addr = de_vstart;
  out.de_ve_addr = de_vstart + d.v_active - 1;

  // Calculate and Store HSync horizontal and vertical start/stop times
  const uint32_t hstart = (de_hstart + d.h_period - d.hsync_bp - d.hsync_width) % d.h_period;
  const uint32_t hend = (de_hstart + d.h_period - d.hsync_bp) % d.h_period;
  out.hs_hs_addr = hstart;
  out.hs_he_addr = hend;
  out.hs_vs_addr = 0;
  out.hs_ve_addr = d.v_period - 1;

  // Calculate and Store VSync horizontal and vertical start/stop times
  out.vs_hs_addr = (hstart + d.h_period) % d.h_period;
  out.vs_he_addr = out.vs_hs_addr;
  const uint32_t vstart = (de_vstart + d.v_period - d.vsync_bp - d.vsync_width) % d.v_period;
  const uint32_t vend = (de_vstart + d.v_period - d.vsync_bp) % d.v_period;
  out.vs_vs_addr = vstart;
  out.vs_ve_addr = vend;
  return out;
}

zx_status_t Clock::PllLockWait() {
  uint32_t pll_lock;

  for (int lock_attempts = 0; lock_attempts < kMaxPllLockAttempt; lock_attempts++) {
    DISP_TRACE("Waiting for PLL Lock: (%d/3).", lock_attempts + 1);
    if (lock_attempts == 1) {
      SET_BIT32(HHI, HHI_HDMI_PLL_CNTL3, 1, 31, 1);
    } else if (lock_attempts == 2) {
      WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL6, 0x55540000);  // more magic
    }
    int retries = 1000;
    while ((pll_lock = GET_BIT32(HHI, HHI_HDMI_PLL_CNTL0, LCD_PLL_LOCK_HPLL_G12A, 1)) != 1 &&
           retries--) {
      zx_nanosleep(zx_deadline_after(ZX_USEC(50)));
    }
    if (pll_lock) {
      return ZX_OK;
    }
  }

  // We got here, which means we never locked!
  DISP_ERROR("PLL not locked! exiting");
  return ZX_ERR_UNAVAILABLE;
}

// static
zx::status<PllConfig> Clock::GenerateHPLL(const display_setting_t& d) {
  PllConfig pll_cfg;
  uint32_t pll_fout;
  // Requested Pixel clock
  pll_cfg.fout = d.lcd_clock / kKHZ;  // KHz
  if (pll_cfg.fout > MAX_PIXEL_CLK_KHZ) {
    DISP_ERROR("Pixel clock out of range (%u KHz)", pll_cfg.fout);
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  constexpr uint32_t kMinClockFactor = 1u;
  constexpr uint32_t kMaxClockFactor = 255u;

  // If clock factor is not specified in display panel configuration, the driver
  // will find the first valid clock factor in between kMinClockFactor and
  // kMaxClockFactor (both inclusive).
  uint32_t clock_factor_min = kMinClockFactor;
  uint32_t clock_factor_max = kMaxClockFactor;
  if (d.clock_factor) {
    clock_factor_min = clock_factor_max = d.clock_factor;
  }

  for (uint32_t clock_factor = clock_factor_min; clock_factor <= clock_factor_max; clock_factor++) {
    pll_cfg.clock_factor = clock_factor;

    // Desired PLL Frequency based on pixel clock needed
    pll_fout = pll_cfg.fout * pll_cfg.clock_factor;

    // Make sure all clocks are within range
    // If these values are not within range, we will not have a valid display
    uint32_t dsi_bit_rate_max_khz = d.bit_rate_max * 1000;  // change to KHz
    uint32_t dsi_bit_rate_min_khz = dsi_bit_rate_max_khz - pll_cfg.fout;
    if ((pll_fout < dsi_bit_rate_min_khz) || (pll_fout > dsi_bit_rate_max_khz)) {
      DISP_TRACE("Calculated clocks out of range for xd = %u, skipped", clock_factor);
      continue;
    }

    // Now that we have valid frequency ranges, let's calculated all the PLL-related
    // multipliers/dividers
    // [fin] * [m/n] = [pll_vco]
    // [pll_vco] / [od1] / [od2] / [od3] = pll_fout
    // [fvco] --->[OD1] --->[OD2] ---> [OD3] --> pll_fout
    uint32_t od3, od2, od1;
    od3 = (1 << (MAX_OD_SEL - 1));
    while (od3) {
      uint32_t fod3 = pll_fout * od3;
      od2 = od3;
      while (od2) {
        uint32_t fod2 = fod3 * od2;
        od1 = od2;
        while (od1) {
          uint32_t fod1 = fod2 * od1;
          if ((fod1 >= MIN_PLL_VCO_KHZ) && (fod1 <= MAX_PLL_VCO_KHZ)) {
            // within range!
            pll_cfg.pll_od1_sel = od1 >> 1;
            pll_cfg.pll_od2_sel = od2 >> 1;
            pll_cfg.pll_od3_sel = od3 >> 1;
            pll_cfg.pll_fout = pll_fout;
            DISP_TRACE("od1=%d, od2=%d, od3=%d", (od1 >> 1), (od2 >> 1), (od3 >> 1));
            DISP_TRACE("pll_fvco=%d", fod1);
            pll_cfg.pll_fvco = fod1;
            // for simplicity, assume n = 1
            // calculate m such that fin x m = fod1
            uint32_t m;
            uint32_t pll_frac;
            fod1 = fod1 / 1;
            m = fod1 / FIN_FREQ_KHZ;
            pll_frac = (fod1 % FIN_FREQ_KHZ) * PLL_FRAC_RANGE / FIN_FREQ_KHZ;
            pll_cfg.pll_m = m;
            pll_cfg.pll_n = 1;
            pll_cfg.pll_frac = pll_frac;
            DISP_TRACE("m=%d, n=%d, frac=0x%x", m, 1, pll_frac);
            pll_cfg.bitrate = pll_fout * kKHZ;  // Hz
            return zx::ok(std::move(pll_cfg));
          }
          od1 >>= 1;
        }
        od2 >>= 1;
      }
      od3 >>= 1;
    }
  }

  DISP_ERROR("Could not generate correct PLL values!");
  DumpDisplaySettings(d);
  return zx::error(ZX_ERR_INTERNAL);
}

void Clock::Disable() {
  if (!clock_enabled_) {
    return;
  }
  WRITE32_REG(VPU, ENCL_VIDEO_EN, 0);

  SET_BIT32(HHI, HHI_VID_CLK_CNTL2, 0, ENCL_GATE_VCLK, 1);
  SET_BIT32(HHI, HHI_VIID_CLK_CNTL, 0, 0, 5);
  SET_BIT32(HHI, HHI_VIID_CLK_CNTL, 0, VCLK2_EN, 1);

  // disable pll
  SET_BIT32(HHI, HHI_HDMI_PLL_CNTL0, 0, LCD_PLL_EN_HPLL_G12A, 1);
  clock_enabled_ = false;
}

zx_status_t Clock::Enable(const display_setting_t& d) {
  if (clock_enabled_) {
    return ZX_OK;
  }

  // Populate internal LCD timing structure based on predefined tables
  lcd_timing_ = CalculateLcdTiming(d);
  auto pll_result = GenerateHPLL(d);
  if (!pll_result.is_error()) {
    pll_cfg_ = std::move(pll_result.value());
    last_valid_display_settings_ = d;
  } else {
    DISP_ERROR("PLL generation failed, using the old config");
    Dump();
  }

  uint32_t regVal;
  PllConfig* pll_cfg = &pll_cfg_;
  bool useFrac = !!pll_cfg->pll_frac;

  regVal = ((1 << LCD_PLL_EN_HPLL_G12A) | (1 << LCD_PLL_OUT_GATE_CTRL_G12A) |  // clk out gate
            (pll_cfg->pll_n << LCD_PLL_N_HPLL_G12A) | (pll_cfg->pll_m << LCD_PLL_M_HPLL_G12A) |
            (pll_cfg->pll_od1_sel << LCD_PLL_OD1_HPLL_G12A) |
            (pll_cfg->pll_od2_sel << LCD_PLL_OD2_HPLL_G12A) |
            (pll_cfg->pll_od3_sel << LCD_PLL_OD3_HPLL_G12A) | (useFrac ? (1 << 27) : (0 << 27)));
  WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL0, regVal);

  WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, pll_cfg->pll_frac);
  WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x00);
  // Magic numbers from U-Boot.
  WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, useFrac ? 0x6a285c00 : 0x48681c00);
  WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, useFrac ? 0x65771290 : 0x33771290);
  WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x39272000);
  WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL6, useFrac ? 0x56540000 : 0x56540000);

  // reset dpll
  SET_BIT32(HHI, HHI_HDMI_PLL_CNTL0, 1, LCD_PLL_RST_HPLL_G12A, 1);
  zx_nanosleep(zx_deadline_after(ZX_USEC(100)));
  // release from reset
  SET_BIT32(HHI, HHI_HDMI_PLL_CNTL0, 0, LCD_PLL_RST_HPLL_G12A, 1);

  zx_nanosleep(zx_deadline_after(ZX_USEC(50)));
  zx_status_t status = PllLockWait();
  if (status != ZX_OK) {
    DISP_ERROR("hpll lock failed");
    return status;
  }

  // Enable VIID Clock (whatever that is)
  SET_BIT32(HHI, HHI_VIID_CLK_CNTL, 0, VCLK2_EN, 1);
  zx_nanosleep(zx_deadline_after(ZX_USEC(5)));

  // Disable the div output clock
  SET_BIT32(HHI, HHI_VID_PLL_CLK_DIV, 0, 19, 1);
  SET_BIT32(HHI, HHI_VID_PLL_CLK_DIV, 0, 15, 1);

  SET_BIT32(HHI, HHI_VID_PLL_CLK_DIV, 1, 18, 1);  // Undocumented register bit

  // Enable the final output clock
  SET_BIT32(HHI, HHI_VID_PLL_CLK_DIV, 1, 19, 1);  // Undocumented register bit

  // Undocumented register bits
  SET_BIT32(HHI, HHI_VDIN_MEAS_CLK_CNTL, 0, 21, 3);
  SET_BIT32(HHI, HHI_VDIN_MEAS_CLK_CNTL, 0, 12, 7);
  SET_BIT32(HHI, HHI_VDIN_MEAS_CLK_CNTL, 1, 20, 1);

  // USE VID_PLL
  SET_BIT32(HHI, HHI_MIPIDSI_PHY_CLK_CNTL, 0, 12, 3);
  // enable dsi_phy_clk
  SET_BIT32(HHI, HHI_MIPIDSI_PHY_CLK_CNTL, 1, 8, 1);
  // set divider to 0 -- undocumented
  SET_BIT32(HHI, HHI_MIPIDSI_PHY_CLK_CNTL, 0, 0, 7);

  // setup the XD divider value
  SET_BIT32(HHI, HHI_VIID_CLK_DIV, (pll_cfg_.clock_factor - 1), VCLK2_XD, 8);
  zx_nanosleep(zx_deadline_after(ZX_USEC(5)));

  // select vid_pll_clk
  SET_BIT32(HHI, HHI_VIID_CLK_CNTL, 0, VCLK2_CLK_IN_SEL, 3);
  SET_BIT32(HHI, HHI_VIID_CLK_CNTL, 1, VCLK2_EN, 1);
  zx_nanosleep(zx_deadline_after(ZX_USEC(2)));

  // [15:12] encl_clk_sel, select vclk2_div1
  SET_BIT32(HHI, HHI_VIID_CLK_DIV, 8, ENCL_CLK_SEL, 4);
  // release vclk2_div_reset and enable vclk2_div
  SET_BIT32(HHI, HHI_VIID_CLK_DIV, 1, VCLK2_XD_EN, 2);
  zx_nanosleep(zx_deadline_after(ZX_USEC(5)));

  SET_BIT32(HHI, HHI_VIID_CLK_CNTL, 1, VCLK2_DIV1_EN, 1);
  SET_BIT32(HHI, HHI_VIID_CLK_CNTL, 1, VCLK2_SOFT_RST, 1);
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
  SET_BIT32(HHI, HHI_VIID_CLK_CNTL, 0, VCLK2_SOFT_RST, 1);
  zx_nanosleep(zx_deadline_after(ZX_USEC(5)));

  // enable CTS_ENCL clk gate
  SET_BIT32(HHI, HHI_VID_CLK_CNTL2, 1, ENCL_GATE_VCLK, 1);

  zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));

  WRITE32_REG(VPU, ENCL_VIDEO_EN, 0);

  // connect both VIUs (Video Input Units) to LCD LVDS Encoders
  WRITE32_REG(VPU, VPU_VIU_VENC_MUX_CTRL, (0 << 0) | (0 << 2));  // TODO(payamm): macros

  // Undocumented registers below
  WRITE32_REG(VPU, ENCL_VIDEO_MODE, 0x8000);      // bit[15] shadown en
  WRITE32_REG(VPU, ENCL_VIDEO_MODE_ADV, 0x0418);  // Sampling rate: 1

  // bypass filter -- Undocumented registers
  WRITE32_REG(VPU, ENCL_VIDEO_FILT_CTRL, 0x1000);
  WRITE32_REG(VPU, ENCL_VIDEO_MAX_PXCNT, d.h_period - 1);
  WRITE32_REG(VPU, ENCL_VIDEO_MAX_LNCNT, d.v_period - 1);
  WRITE32_REG(VPU, ENCL_VIDEO_HAVON_BEGIN, lcd_timing_.vid_pixel_on);
  WRITE32_REG(VPU, ENCL_VIDEO_HAVON_END, d.h_active - 1 + lcd_timing_.vid_pixel_on);
  WRITE32_REG(VPU, ENCL_VIDEO_VAVON_BLINE, lcd_timing_.vid_line_on);
  WRITE32_REG(VPU, ENCL_VIDEO_VAVON_ELINE, d.v_active - 1 + lcd_timing_.vid_line_on);
  WRITE32_REG(VPU, ENCL_VIDEO_HSO_BEGIN, lcd_timing_.hs_hs_addr);
  WRITE32_REG(VPU, ENCL_VIDEO_HSO_END, lcd_timing_.hs_he_addr);
  WRITE32_REG(VPU, ENCL_VIDEO_VSO_BEGIN, lcd_timing_.vs_hs_addr);
  WRITE32_REG(VPU, ENCL_VIDEO_VSO_END, lcd_timing_.vs_he_addr);
  WRITE32_REG(VPU, ENCL_VIDEO_VSO_BLINE, lcd_timing_.vs_vs_addr);
  WRITE32_REG(VPU, ENCL_VIDEO_VSO_ELINE, lcd_timing_.vs_ve_addr);
  WRITE32_REG(VPU, ENCL_VIDEO_RGBIN_CTRL, 3);
  WRITE32_REG(VPU, ENCL_VIDEO_EN, 1);

  WRITE32_REG(VPU, L_RGB_BASE_ADDR, 0);
  WRITE32_REG(VPU, L_RGB_COEFF_ADDR, 0x400);
  WRITE32_REG(VPU, L_DITH_CNTL_ADDR, 0x400);

  // DE signal for TTL m8,m8m2
  WRITE32_REG(VPU, L_OEH_HS_ADDR, lcd_timing_.de_hs_addr);
  WRITE32_REG(VPU, L_OEH_HE_ADDR, lcd_timing_.de_he_addr);
  WRITE32_REG(VPU, L_OEH_VS_ADDR, lcd_timing_.de_vs_addr);
  WRITE32_REG(VPU, L_OEH_VE_ADDR, lcd_timing_.de_ve_addr);
  // DE signal for TTL m8b
  WRITE32_REG(VPU, L_OEV1_HS_ADDR, lcd_timing_.de_hs_addr);
  WRITE32_REG(VPU, L_OEV1_HE_ADDR, lcd_timing_.de_he_addr);
  WRITE32_REG(VPU, L_OEV1_VS_ADDR, lcd_timing_.de_vs_addr);
  WRITE32_REG(VPU, L_OEV1_VE_ADDR, lcd_timing_.de_ve_addr);

  // Hsync signal for TTL m8,m8m2
  if (d.hsync_pol == 0) {
    WRITE32_REG(VPU, L_STH1_HS_ADDR, lcd_timing_.hs_he_addr);
    WRITE32_REG(VPU, L_STH1_HE_ADDR, lcd_timing_.hs_hs_addr);
  } else {
    WRITE32_REG(VPU, L_STH1_HS_ADDR, lcd_timing_.hs_hs_addr);
    WRITE32_REG(VPU, L_STH1_HE_ADDR, lcd_timing_.hs_he_addr);
  }
  WRITE32_REG(VPU, L_STH1_VS_ADDR, lcd_timing_.hs_vs_addr);
  WRITE32_REG(VPU, L_STH1_VE_ADDR, lcd_timing_.hs_ve_addr);

  // Vsync signal for TTL m8,m8m2
  WRITE32_REG(VPU, L_STV1_HS_ADDR, lcd_timing_.vs_hs_addr);
  WRITE32_REG(VPU, L_STV1_HE_ADDR, lcd_timing_.vs_he_addr);
  if (d.vsync_pol == 0) {
    WRITE32_REG(VPU, L_STV1_VS_ADDR, lcd_timing_.vs_ve_addr);
    WRITE32_REG(VPU, L_STV1_VE_ADDR, lcd_timing_.vs_vs_addr);
  } else {
    WRITE32_REG(VPU, L_STV1_VS_ADDR, lcd_timing_.vs_vs_addr);
    WRITE32_REG(VPU, L_STV1_VE_ADDR, lcd_timing_.vs_ve_addr);
  }

  // DE signal
  WRITE32_REG(VPU, L_DE_HS_ADDR, lcd_timing_.de_hs_addr);
  WRITE32_REG(VPU, L_DE_HE_ADDR, lcd_timing_.de_he_addr);
  WRITE32_REG(VPU, L_DE_VS_ADDR, lcd_timing_.de_vs_addr);
  WRITE32_REG(VPU, L_DE_VE_ADDR, lcd_timing_.de_ve_addr);

  // Hsync signal
  WRITE32_REG(VPU, L_HSYNC_HS_ADDR, lcd_timing_.hs_hs_addr);
  WRITE32_REG(VPU, L_HSYNC_HE_ADDR, lcd_timing_.hs_he_addr);
  WRITE32_REG(VPU, L_HSYNC_VS_ADDR, lcd_timing_.hs_vs_addr);
  WRITE32_REG(VPU, L_HSYNC_VE_ADDR, lcd_timing_.hs_ve_addr);

  // Vsync signal
  WRITE32_REG(VPU, L_VSYNC_HS_ADDR, lcd_timing_.vs_hs_addr);
  WRITE32_REG(VPU, L_VSYNC_HE_ADDR, lcd_timing_.vs_he_addr);
  WRITE32_REG(VPU, L_VSYNC_VS_ADDR, lcd_timing_.vs_vs_addr);
  WRITE32_REG(VPU, L_VSYNC_VE_ADDR, lcd_timing_.vs_ve_addr);

  WRITE32_REG(VPU, L_INV_CNT_ADDR, 0);
  WRITE32_REG(VPU, L_TCON_MISC_SEL_ADDR, ((1 << kStv1Sel) | (1 << kStv2Sel)));

  WRITE32_REG(VPU, VPP_MISC, READ32_REG(VPU, VPP_MISC) & ~(VPP_OUT_SATURATE));

  // Ready to be used
  clock_enabled_ = true;
  return ZX_OK;
}

void Clock::SetVideoOn(bool on) { WRITE32_REG(VPU, ENCL_VIDEO_EN, on); }

// static
zx::status<std::unique_ptr<Clock>> Clock::Create(ddk::PDev& pdev, bool already_enabled) {
  fbl::AllocChecker ac;
  auto self = fbl::make_unique_checked<Clock>(&ac);
  if (!ac.check()) {
    DISP_ERROR("Clock: could not allocate memory");
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  // Map VPU and HHI registers
  zx_status_t status = pdev.MapMmio(MMIO_VPU, &(self->vpu_mmio_));
  if (status != ZX_OK) {
    DISP_ERROR("Clock: Could not map VPU mmio");
    return zx::error(status);
  }

  status = pdev.MapMmio(MMIO_HHI, &(self->hhi_mmio_));
  if (status != ZX_OK) {
    DISP_ERROR("Clock: Could not map HHI mmio");
    return zx::error(status);
  }
  self->clock_enabled_ = already_enabled;

  return zx::ok(std::move(self));
}

void Clock::Dump() {
  DumpPllCfg(pll_cfg_);
  DumpLcdTiming(lcd_timing_);
  DumpDisplaySettings(last_valid_display_settings_);
}

}  // namespace amlogic_display
