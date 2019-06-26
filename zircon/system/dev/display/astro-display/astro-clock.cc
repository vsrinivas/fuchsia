// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "astro-clock.h"

#include <ddk/debug.h>

namespace astro_display {

namespace {
constexpr uint8_t kMaxPllLockAttempt = 3;
constexpr uint8_t kStv2Sel = 5;
constexpr uint8_t kStv1Sel = 4;
constexpr uint32_t kKHZ = 1000;
}  // namespace

#define READ32_HHI_REG(a) hhi_mmio_->Read32(a)
#define WRITE32_HHI_REG(a, v) hhi_mmio_->Write32(v, a)

#define READ32_VPU_REG(a) vpu_mmio_->Read32(a)
#define WRITE32_VPU_REG(a, v) vpu_mmio_->Write32(v, a)

void AstroDisplayClock::CalculateLcdTiming(const display_setting_t& d) {
  // Calculate and store DataEnable horizontal and vertical start/stop times
  const uint32_t de_hstart = d.h_period - d.h_active - 1;
  const uint32_t de_vstart = d.v_period - d.v_active;
  lcd_timing_.vid_pixel_on = de_hstart;
  lcd_timing_.vid_line_on = de_vstart;
  lcd_timing_.de_hs_addr = de_hstart;
  lcd_timing_.de_he_addr = de_hstart + d.h_active;
  lcd_timing_.de_vs_addr = de_vstart;
  lcd_timing_.de_ve_addr = de_vstart + d.v_active - 1;

  // Calculate and Store HSync horizontal and vertical start/stop times
  const uint32_t hstart = (de_hstart + d.h_period - d.hsync_bp - d.hsync_width) % d.h_period;
  const uint32_t hend = (de_hstart + d.h_period - d.hsync_bp) % d.h_period;
  lcd_timing_.hs_hs_addr = hstart;
  lcd_timing_.hs_he_addr = hend;
  lcd_timing_.hs_vs_addr = 0;
  lcd_timing_.hs_ve_addr = d.v_period - 1;

  // Calculate and Store VSync horizontal and vertical start/stop times
  lcd_timing_.vs_hs_addr = (hstart + d.h_period) % d.h_period;
  lcd_timing_.vs_he_addr = lcd_timing_.vs_hs_addr;
  const uint32_t vstart = (de_vstart + d.v_period - d.vsync_bp - d.vsync_width) % d.v_period;
  const uint32_t vend = (de_vstart + d.v_period - d.vsync_bp) % d.v_period;
  lcd_timing_.vs_vs_addr = vstart;
  lcd_timing_.vs_ve_addr = vend;
}

zx_status_t AstroDisplayClock::PllLockWait() {
  uint32_t pll_lock;

  for (int lock_attempts = 0; lock_attempts < kMaxPllLockAttempt; lock_attempts++) {
    DISP_SPEW("Waiting for PLL Lock: (%d/3).\n", lock_attempts + 1);
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
  DISP_ERROR("PLL not locked! exiting\n");
  return ZX_ERR_UNAVAILABLE;
}

zx_status_t AstroDisplayClock::GenerateHPLL(const display_setting_t& d) {
  uint32_t pll_fout;
  // Requested Pixel clock
  pll_cfg_.fout = d.lcd_clock / kKHZ;  // KHz
  // Desired PLL Frequency based on pixel clock needed
  pll_fout = pll_cfg_.fout * d.clock_factor;

  // Make sure all clocks are within range
  // If these values are not within range, we will not have a valid display
  if ((pll_cfg_.fout > MAX_PIXEL_CLK_KHZ) || (pll_fout < MIN_PLL_FREQ_KHZ) ||
      (pll_fout > MAX_PLL_FREQ_KHZ)) {
    DISP_ERROR("Calculated clocks out of range!\n");
    return ZX_ERR_OUT_OF_RANGE;
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
          pll_cfg_.pll_od1_sel = od1 >> 1;
          pll_cfg_.pll_od2_sel = od2 >> 1;
          pll_cfg_.pll_od3_sel = od3 >> 1;
          pll_cfg_.pll_fout = pll_fout;
          DISP_SPEW("od1=%d, od2=%d, od3=%d\n", (od1 >> 1), (od2 >> 1), (od3 >> 1));
          DISP_SPEW("pll_fvco=%d\n", fod1);
          pll_cfg_.pll_fvco = fod1;
          // for simplicity, assume n = 1
          // calculate m such that fin x m = fod1
          uint32_t m;
          uint32_t pll_frac;
          fod1 = fod1 / 1;
          m = fod1 / FIN_FREQ_KHZ;
          pll_frac = (fod1 % FIN_FREQ_KHZ) * PLL_FRAC_RANGE / FIN_FREQ_KHZ;
          pll_cfg_.pll_m = m;
          pll_cfg_.pll_n = 1;
          pll_cfg_.pll_frac = pll_frac;
          DISP_SPEW("m=%d, n=%d, frac=0x%x\n", m, 1, pll_frac);
          pll_cfg_.bitrate = pll_fout * kKHZ;  // Hz
          return ZX_OK;
        }
        od1 >>= 1;
      }
      od2 >>= 1;
    }
    od3 >>= 1;
  }

  DISP_ERROR("Could not generate correct PLL values!\n");
  return ZX_ERR_INTERNAL;
}

void AstroDisplayClock::Disable() {
  ZX_DEBUG_ASSERT(initialized_);
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

zx_status_t AstroDisplayClock::Enable(const display_setting_t& d) {
  ZX_DEBUG_ASSERT(initialized_);

  if (clock_enabled_) {
    return ZX_OK;
  }

  // Populate internal LCD timing structure based on predefined tables
  CalculateLcdTiming(d);
  GenerateHPLL(d);

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
    DISP_ERROR("hpll lock failed\n");
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
  SET_BIT32(HHI, HHI_VIID_CLK_DIV, (d.clock_factor - 1), VCLK2_XD, 8);
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

zx_status_t AstroDisplayClock::Init(zx_device_t* parent) {
  if (initialized_) {
    return ZX_OK;
  }

  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev_);
  if (status != ZX_OK) {
    DISP_ERROR("AstroDisplayClock: Could not get ZX_PROTOCOL_PDEV protocol\n");
    return status;
  }

  // Map VPU and HHI registers
  mmio_buffer_t mmio;
  status = pdev_map_mmio_buffer(&pdev_, MMIO_VPU, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("AstroDisplayClock: Could not map VPU mmio\n");
    return status;
  }
  vpu_mmio_ = ddk::MmioBuffer(mmio);

  status = pdev_map_mmio_buffer(&pdev_, MMIO_HHI, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("AstroDisplayClock: Could not map HHI mmio\n");
    return status;
  }
  hhi_mmio_ = ddk::MmioBuffer(mmio);

  initialized_ = true;
  return ZX_OK;
}

void AstroDisplayClock::Dump() {
  ZX_DEBUG_ASSERT(initialized_);
  DISP_INFO("#############################\n");
  DISP_INFO("Dumping pll_cfg structure:\n");
  DISP_INFO("#############################\n");
  DISP_INFO("fin = 0x%x (%u)\n", pll_cfg_.fin, pll_cfg_.fin);
  DISP_INFO("fout = 0x%x (%u)\n", pll_cfg_.fout, pll_cfg_.fout);
  DISP_INFO("pll_m = 0x%x (%u)\n", pll_cfg_.pll_m, pll_cfg_.pll_m);
  DISP_INFO("pll_n = 0x%x (%u)\n", pll_cfg_.pll_n, pll_cfg_.pll_n);
  DISP_INFO("pll_fvco = 0x%x (%u)\n", pll_cfg_.pll_fvco, pll_cfg_.pll_fvco);
  DISP_INFO("pll_od1_sel = 0x%x (%u)\n", pll_cfg_.pll_od1_sel, pll_cfg_.pll_od1_sel);
  DISP_INFO("pll_od2_sel = 0x%x (%u)\n", pll_cfg_.pll_od2_sel, pll_cfg_.pll_od2_sel);
  DISP_INFO("pll_od3_sel = 0x%x (%u)\n", pll_cfg_.pll_od3_sel, pll_cfg_.pll_od3_sel);
  DISP_INFO("pll_frac = 0x%x (%u)\n", pll_cfg_.pll_frac, pll_cfg_.pll_frac);
  DISP_INFO("pll_fout = 0x%x (%u)\n", pll_cfg_.pll_fout, pll_cfg_.pll_fout);

  DISP_INFO("#############################\n");
  DISP_INFO("Dumping lcd_timing structure:\n");
  DISP_INFO("#############################\n");
  DISP_INFO("vid_pixel_on = 0x%x (%u)\n", lcd_timing_.vid_pixel_on, lcd_timing_.vid_pixel_on);
  DISP_INFO("vid_line_on = 0x%x (%u)\n", lcd_timing_.vid_line_on, lcd_timing_.vid_line_on);
  DISP_INFO("de_hs_addr = 0x%x (%u)\n", lcd_timing_.de_hs_addr, lcd_timing_.de_hs_addr);
  DISP_INFO("de_he_addr = 0x%x (%u)\n", lcd_timing_.de_he_addr, lcd_timing_.de_he_addr);
  DISP_INFO("de_vs_addr = 0x%x (%u)\n", lcd_timing_.de_vs_addr, lcd_timing_.de_vs_addr);
  DISP_INFO("de_ve_addr = 0x%x (%u)\n", lcd_timing_.de_ve_addr, lcd_timing_.de_ve_addr);
  DISP_INFO("hs_hs_addr = 0x%x (%u)\n", lcd_timing_.hs_hs_addr, lcd_timing_.hs_hs_addr);
  DISP_INFO("hs_he_addr = 0x%x (%u)\n", lcd_timing_.hs_he_addr, lcd_timing_.hs_he_addr);
  DISP_INFO("hs_vs_addr = 0x%x (%u)\n", lcd_timing_.hs_vs_addr, lcd_timing_.hs_vs_addr);
  DISP_INFO("hs_ve_addr = 0x%x (%u)\n", lcd_timing_.hs_ve_addr, lcd_timing_.hs_ve_addr);
  DISP_INFO("vs_hs_addr = 0x%x (%u)\n", lcd_timing_.vs_hs_addr, lcd_timing_.vs_hs_addr);
  DISP_INFO("vs_he_addr = 0x%x (%u)\n", lcd_timing_.vs_he_addr, lcd_timing_.vs_he_addr);
  DISP_INFO("vs_vs_addr = 0x%x (%u)\n", lcd_timing_.vs_vs_addr, lcd_timing_.vs_vs_addr);
  DISP_INFO("vs_ve_addr = 0x%x (%u)\n", lcd_timing_.vs_ve_addr, lcd_timing_.vs_ve_addr);
}

}  // namespace astro_display
