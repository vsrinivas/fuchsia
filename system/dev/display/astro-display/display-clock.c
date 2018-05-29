// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "astro-display.h"

#define STV2_SEL         5
#define STV1_SEL         4

// This function populates lcd timings based on input display settings
static void  astro_lcd_timing(astro_display_t* display) {
    uint32_t de_hstart, de_vstart;
    uint32_t hstart, hend, vstart, vend;
    uint32_t hPeriod, vPeriod, hActive, vActive;
    uint32_t hsync_bp, hsync_width, vsync_width, vsync_bp;

    // local copies for easier calculation
    hPeriod = display->disp_setting.h_period;
    vPeriod = display->disp_setting.v_period;
    hActive = display->disp_setting.h_active;
    vActive = display->disp_setting.v_active;
    hsync_width = display->disp_setting.hsync_width;
    hsync_bp = display->disp_setting.hsync_bp;
    vsync_width = display->disp_setting.vsync_width;
    vsync_bp = display->disp_setting.vsync_bp;

    // Calculate and store DataEnable horizontal and vertical start/stop times
    de_hstart = hPeriod - hActive - 1;
    de_vstart = vPeriod - vActive;
    display->lcd_timing.vid_pixel_on = de_hstart;
    display->lcd_timing.vid_line_on = de_vstart;
    display->lcd_timing.de_hs_addr = de_hstart;
    display->lcd_timing.de_he_addr = de_hstart + hActive;
    display->lcd_timing.de_vs_addr = de_vstart;
    display->lcd_timing.de_ve_addr = de_vstart + vActive - 1;

    // Calculate and Store HSync horizontal and vertical start/stop times
    hstart = (de_hstart + hPeriod - hsync_bp - hsync_width) % hPeriod;
    hend = (de_hstart + hPeriod - hsync_bp) % hPeriod;
    display->lcd_timing.hs_hs_addr = hstart;
    display->lcd_timing.hs_he_addr = hend;
    display->lcd_timing.hs_vs_addr = 0;
    display->lcd_timing.hs_ve_addr = vPeriod - 1;

    // Calculate and Store VSync horizontal and vertical start/stop times
    display->lcd_timing.vs_hs_addr = (hstart + hPeriod) % hPeriod;
    display->lcd_timing.vs_he_addr = display->lcd_timing.vs_hs_addr;
    vstart = (de_vstart + vPeriod - vsync_bp - vsync_width) % vPeriod;
    vend = (de_vstart + vPeriod - vsync_bp) % vPeriod;
    display->lcd_timing.vs_vs_addr = vstart;
    display->lcd_timing.vs_ve_addr = vend;
}

/* This function wait for hdmi_pll to lock. The retry algorithm is
 * undocumented and comes from U-Boot.
 */
#define MAX_PLL_LOCK_ATTEMPT 3
static zx_status_t pll_lock_wait(astro_display_t* display)
{
    uint32_t pll_lock;
    zx_status_t status = ZX_OK;

    for (int lock_attempts = 0; lock_attempts < MAX_PLL_LOCK_ATTEMPT; lock_attempts++) {
        DISP_SPEW("Waiting for PLL Lock: (%d/3).\n", lock_attempts+1);
        if (lock_attempts == 1) {
            SET_BIT32(HHI, HHI_HDMI_PLL_CNTL3, 1, 31, 1);
        } else if (lock_attempts == 2) {
            WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL6, 0x55540000); // more magic
        }
        int retries = 1000;
        while ((pll_lock = GET_BIT32(HHI, HHI_HDMI_PLL_CNTL0, LCD_PLL_LOCK_HPLL_G12A, 1)) != 1 &&
               retries--) {
            usleep(50);
        }
        if (pll_lock) break;
    }

    if (pll_lock != 1) {
        DISP_ERROR("PLL not locked! exiting\n");
        status = ZX_ERR_UNAVAILABLE;
    }
    return status;
}

// This function calculates the required pll configurations needed to generate
// the desired lcd clock
static zx_status_t astro_dsi_generate_hpll(astro_display_t* display) {
    uint32_t pll_fout;
    // Requested Pixel clock
    display->pll_cfg.fout = display->disp_setting.lcd_clock / 1000; // KHz
    // Desired PLL Frequency based on pixel clock needed
    pll_fout = display->pll_cfg.fout * display->disp_setting.clock_factor;

    // Make sure all clocks are within range
    // If these values are not within range, we will not have a valid display
    if((display->pll_cfg.fout > MAX_PIXEL_CLK_KHZ) ||
       (pll_fout < MIN_PLL_FREQ_KHZ) || (pll_fout > MAX_PLL_FREQ_KHZ)) {
        DISP_ERROR("Calculated clocks out of range!\n");
        return ZX_ERR_OUT_OF_RANGE;
    }

    // Now that we have valid frequency ranges, let's calculated all the PLL-related
    // multipliers/dividers
    // [fin] * [m/n] = [pll_vco]
    // [pll_vco] / [od1] / [od2] / [od3] = pll_fout
    // [fvco] --->[OD1] --->[OD2] ---> [OD3] --> pll_fout
    bool valid_calc = false;
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
                if ((fod1 >= MIN_PLL_VCO_KHZ) &&
                    (fod1 <= MAX_PLL_VCO_KHZ)) {
                    // within range!
                    display->pll_cfg.pll_od1_sel = od1 >> 1;
                    display->pll_cfg.pll_od2_sel = od2 >> 1;
                    display->pll_cfg.pll_od3_sel = od3 >> 1;
                    display->pll_cfg.pll_fout = pll_fout;
                    DISP_SPEW("od1=%d, od2=%d, od3=%d\n",
                            (od1 >> 1), (od2 >> 1),
                            (od3 >> 1));
                    DISP_SPEW("pll_fvco=%d\n", fod1);
                    display->pll_cfg.pll_fvco = fod1;
                    // for simplicity, assume n = 1
                    // calculate m such that fin x m = fod1
                    uint32_t m;
                    uint32_t pll_frac;
                    fod1 = fod1 / 1;
                    m = fod1 / FIN_FREQ_KHZ;
                    pll_frac = (fod1 % FIN_FREQ_KHZ) * PLL_FRAC_RANGE / FIN_FREQ_KHZ;
                    display->pll_cfg.pll_m = m;
                    display->pll_cfg.pll_n = 1;
                    display->pll_cfg.pll_frac = pll_frac;
                    DISP_SPEW("m=%d, n=%d, frac=0x%x\n",
                            m, 1, pll_frac);
                    valid_calc = true;
                    goto calc_done;
                }
                od1 >>= 1;
            }
            od2 >>= 1;
        }
        od3 >>= 1;
    }

calc_done:
    if (!valid_calc) {
        DISP_ERROR("Could not generate correct PLL values!\n");
        return ZX_ERR_INTERNAL;
    }
    display->pll_cfg.bitrate = pll_fout * 1000; // Hz
    return ZX_OK;
}

// This function is a collection various clock related setup done based on
// u-boot. Most of the registers and/or bit fields are undocumented.
zx_status_t display_clock_init(astro_display_t* display) {
    if (display == NULL) {
        DISP_ERROR("Uninitialized memory detected!\n");
        return ZX_ERR_INVALID_ARGS;
    }

    // Populate internal LCD timing structure based on predefined tables
    astro_lcd_timing(display);

    // NOTE: Unforgiving function. Invalid clock values will cause panic
    astro_dsi_generate_hpll(display);

    uint32_t regVal;
    pll_config_t* pll_cfg = &display->pll_cfg;
    bool useFrac = !!pll_cfg->pll_frac;

    regVal =  ((1 << LCD_PLL_EN_HPLL_G12A) |
               (1 << LCD_PLL_OUT_GATE_CTRL_G12A) | /* clk out gate */
               (pll_cfg->pll_n << LCD_PLL_N_HPLL_G12A) |
               (pll_cfg->pll_m << LCD_PLL_M_HPLL_G12A) |
               (pll_cfg->pll_od1_sel << LCD_PLL_OD1_HPLL_G12A) |
               (pll_cfg->pll_od2_sel << LCD_PLL_OD2_HPLL_G12A) |
               (pll_cfg->pll_od3_sel << LCD_PLL_OD3_HPLL_G12A) |
               (useFrac? (1 << 27) : (0 << 27)));
    WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL0, regVal);

    WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL1, pll_cfg->pll_frac);
    WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL2, 0x00);
    // Magic numbers from U-Boot.
    WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL3, useFrac? 0x6a285c00 : 0x48681c00);
    WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL4, useFrac? 0x65771290 : 0x33771290);
    WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL5, 0x39272000);
    WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL6, useFrac? 0x56540000 : 0x56540000);

    // reset dpll
    SET_BIT32(HHI, HHI_HDMI_PLL_CNTL0, 1, LCD_PLL_RST_HPLL_G12A, 1);
    usleep(100);
    // release from reset
    SET_BIT32(HHI, HHI_HDMI_PLL_CNTL0, 0, LCD_PLL_RST_HPLL_G12A, 1);

    usleep(50);
    zx_status_t status = pll_lock_wait(display); // should we be a bit more unforgiving and panic?
    if (status != ZX_OK) {
        DISP_ERROR("hpll lock failed\n");
        return status;
    }

    // Enable VIID Clock (whatever that is)
    SET_BIT32(HHI, HHI_VIID_CLK_CNTL, 0, VCLK2_EN, 1);
    usleep(5);

    /* Disable the div output clock */
    SET_BIT32(HHI, HHI_VID_PLL_CLK_DIV, 0, 19, 1);
    SET_BIT32(HHI, HHI_VID_PLL_CLK_DIV, 0, 15, 1);

    SET_BIT32(HHI, HHI_VID_PLL_CLK_DIV, 1, 18, 1); // Undocumented register bit

    /* Enable the final output clock */
    SET_BIT32(HHI, HHI_VID_PLL_CLK_DIV, 1, 19, 1); // Undocumented register bit

    /* Undocumented register bits*/
    SET_BIT32(HHI, HHI_VDIN_MEAS_CLK_CNTL, 0, 21, 3);
    SET_BIT32(HHI, HHI_VDIN_MEAS_CLK_CNTL, 0, 12, 7);
    SET_BIT32(HHI, HHI_VDIN_MEAS_CLK_CNTL, 1, 20, 1);

    // USE VID_PLL
    SET_BIT32(HHI, HHI_MIPIDSI_PHY_CLK_CNTL, 0, 12, 3);
    // enable dsi_phy_clk
    SET_BIT32(HHI, HHI_MIPIDSI_PHY_CLK_CNTL, 1, 8, 1);
    // set divider to 0 -- undocumented
    SET_BIT32(HHI, HHI_MIPIDSI_PHY_CLK_CNTL, 0, 0, 7);

        /* setup the XD divider value */
    SET_BIT32(HHI, HHI_VIID_CLK_DIV, (display->disp_setting.clock_factor-1), VCLK2_XD, 8);
    usleep(5);

    /* select vid_pll_clk */
    SET_BIT32(HHI, HHI_VIID_CLK_CNTL, 0, VCLK2_CLK_IN_SEL, 3);
    SET_BIT32(HHI, HHI_VIID_CLK_CNTL, 1, VCLK2_EN, 1);
    usleep(2);

    /* [15:12] encl_clk_sel, select vclk2_div1 */
    SET_BIT32(HHI, HHI_VIID_CLK_DIV, 8, ENCL_CLK_SEL, 4);
    /* release vclk2_div_reset and enable vclk2_div */
    SET_BIT32(HHI, HHI_VIID_CLK_DIV, 1, VCLK2_XD_EN, 2);
    usleep(5);

    SET_BIT32(HHI, HHI_VIID_CLK_CNTL, 1, VCLK2_DIV1_EN, 1);
    SET_BIT32(HHI, HHI_VIID_CLK_CNTL, 1, VCLK2_SOFT_RST, 1);
    usleep(10);
    SET_BIT32(HHI, HHI_VIID_CLK_CNTL, 0, VCLK2_SOFT_RST, 1);
    usleep(5);

    /* enable CTS_ENCL clk gate */
    SET_BIT32(HHI, HHI_VID_CLK_CNTL2, 1, ENCL_GATE_VCLK, 1);

    usleep(10000);

    uint32_t h_active, v_active;
    uint32_t video_on_pixel, video_on_line;

    h_active        = display->disp_setting.h_active;
    v_active        = display->disp_setting.v_active;
    video_on_pixel  = display->lcd_timing.vid_pixel_on;
    video_on_line   = display->lcd_timing.vid_line_on;

    WRITE32_REG(VPU, ENCL_VIDEO_EN, 0);

    // connect both VIUs (Video Input Units) to LCD LVDS Encoders
    WRITE32_REG(VPU, VPU_VIU_VENC_MUX_CTRL, (0 << 0) | (0 << 2)); //TODO: macros

    // Undocumented registers below
    WRITE32_REG(VPU, ENCL_VIDEO_MODE, 0x8000); // bit[15] shadown en
    WRITE32_REG(VPU, ENCL_VIDEO_MODE_ADV, 0x0418); // Sampling rate: 1

    // bypass filter -- Undocumented registers
    WRITE32_REG(VPU, ENCL_VIDEO_FILT_CTRL, 0x1000);
    WRITE32_REG(VPU, ENCL_VIDEO_MAX_PXCNT, display->disp_setting.h_period - 1);
    WRITE32_REG(VPU, ENCL_VIDEO_MAX_LNCNT, display->disp_setting.v_period - 1);
    WRITE32_REG(VPU, ENCL_VIDEO_HAVON_BEGIN, video_on_pixel);
    WRITE32_REG(VPU, ENCL_VIDEO_HAVON_END,   h_active - 1 + video_on_pixel);
    WRITE32_REG(VPU, ENCL_VIDEO_VAVON_BLINE, video_on_line);
    WRITE32_REG(VPU, ENCL_VIDEO_VAVON_ELINE, v_active - 1  + video_on_line);
    WRITE32_REG(VPU, ENCL_VIDEO_HSO_BEGIN, display->lcd_timing.hs_hs_addr);
    WRITE32_REG(VPU, ENCL_VIDEO_HSO_END,   display->lcd_timing.hs_he_addr);
    WRITE32_REG(VPU, ENCL_VIDEO_VSO_BEGIN, display->lcd_timing.vs_hs_addr);
    WRITE32_REG(VPU, ENCL_VIDEO_VSO_END,   display->lcd_timing.vs_he_addr);
    WRITE32_REG(VPU, ENCL_VIDEO_VSO_BLINE, display->lcd_timing.vs_vs_addr);
    WRITE32_REG(VPU, ENCL_VIDEO_VSO_ELINE, display->lcd_timing.vs_ve_addr);
    WRITE32_REG(VPU, ENCL_VIDEO_RGBIN_CTRL, 3);
    WRITE32_REG(VPU, ENCL_VIDEO_EN, 1);

    WRITE32_REG(VPU, L_RGB_BASE_ADDR, 0);
    WRITE32_REG(VPU, L_RGB_COEFF_ADDR, 0x400);
    WRITE32_REG(VPU, L_DITH_CNTL_ADDR,  0x400);

    /* DE signal for TTL m8,m8m2 */
    WRITE32_REG(VPU, L_OEH_HS_ADDR, display->lcd_timing.de_hs_addr);
    WRITE32_REG(VPU, L_OEH_HE_ADDR, display->lcd_timing.de_he_addr);
    WRITE32_REG(VPU, L_OEH_VS_ADDR, display->lcd_timing.de_vs_addr);
    WRITE32_REG(VPU, L_OEH_VE_ADDR, display->lcd_timing.de_ve_addr);
    /* DE signal for TTL m8b */
    WRITE32_REG(VPU, L_OEV1_HS_ADDR,  display->lcd_timing.de_hs_addr);
    WRITE32_REG(VPU, L_OEV1_HE_ADDR,  display->lcd_timing.de_he_addr);
    WRITE32_REG(VPU, L_OEV1_VS_ADDR,  display->lcd_timing.de_vs_addr);
    WRITE32_REG(VPU, L_OEV1_VE_ADDR,  display->lcd_timing.de_ve_addr);

    /* Hsync signal for TTL m8,m8m2 */
    if (display->disp_setting.hsync_pol == 0) {
        WRITE32_REG(VPU, L_STH1_HS_ADDR, display->lcd_timing.hs_he_addr);
        WRITE32_REG(VPU, L_STH1_HE_ADDR, display->lcd_timing.hs_hs_addr);
    } else {
        WRITE32_REG(VPU, L_STH1_HS_ADDR, display->lcd_timing.hs_hs_addr);
        WRITE32_REG(VPU, L_STH1_HE_ADDR, display->lcd_timing.hs_he_addr);
    }
    WRITE32_REG(VPU, L_STH1_VS_ADDR, display->lcd_timing.hs_vs_addr);
    WRITE32_REG(VPU, L_STH1_VE_ADDR, display->lcd_timing.hs_ve_addr);

    /* Vsync signal for TTL m8,m8m2 */
    WRITE32_REG(VPU, L_STV1_HS_ADDR, display->lcd_timing.vs_hs_addr);
    WRITE32_REG(VPU, L_STV1_HE_ADDR, display->lcd_timing.vs_he_addr);
    if (display->disp_setting.vsync_pol == 0) {
        WRITE32_REG(VPU, L_STV1_VS_ADDR, display->lcd_timing.vs_ve_addr);
        WRITE32_REG(VPU, L_STV1_VE_ADDR, display->lcd_timing.vs_vs_addr);
    } else {
        WRITE32_REG(VPU, L_STV1_VS_ADDR, display->lcd_timing.vs_vs_addr);
        WRITE32_REG(VPU, L_STV1_VE_ADDR, display->lcd_timing.vs_ve_addr);
    }

    /* DE signal */
    WRITE32_REG(VPU, L_DE_HS_ADDR,    display->lcd_timing.de_hs_addr);
    WRITE32_REG(VPU, L_DE_HE_ADDR,    display->lcd_timing.de_he_addr);
    WRITE32_REG(VPU, L_DE_VS_ADDR,    display->lcd_timing.de_vs_addr);
    WRITE32_REG(VPU, L_DE_VE_ADDR,    display->lcd_timing.de_ve_addr);

    /* Hsync signal */
    WRITE32_REG(VPU, L_HSYNC_HS_ADDR,  display->lcd_timing.hs_hs_addr);
    WRITE32_REG(VPU, L_HSYNC_HE_ADDR,  display->lcd_timing.hs_he_addr);
    WRITE32_REG(VPU, L_HSYNC_VS_ADDR,  display->lcd_timing.hs_vs_addr);
    WRITE32_REG(VPU, L_HSYNC_VE_ADDR,  display->lcd_timing.hs_ve_addr);

    /* Vsync signal */
    WRITE32_REG(VPU, L_VSYNC_HS_ADDR,  display->lcd_timing.vs_hs_addr);
    WRITE32_REG(VPU, L_VSYNC_HE_ADDR,  display->lcd_timing.vs_he_addr);
    WRITE32_REG(VPU, L_VSYNC_VS_ADDR,  display->lcd_timing.vs_vs_addr);
    WRITE32_REG(VPU, L_VSYNC_VE_ADDR,  display->lcd_timing.vs_ve_addr);

    WRITE32_REG(VPU, L_INV_CNT_ADDR, 0);
    WRITE32_REG(VPU, L_TCON_MISC_SEL_ADDR, ((1 << STV1_SEL) | (1 << STV2_SEL)));

    WRITE32_REG(VPU, VPP_MISC, READ32_REG(VPU, VPP_MISC) & ~(VPP_OUT_SATURATE));
    return status;
}

