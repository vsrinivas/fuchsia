// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_ASTRO_DISPLAY_HHI_REGS_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_ASTRO_DISPLAY_HHI_REGS_H_
#define HHI_MIPI_CNTL0 (0x000 << 2)
#define HHI_MIPI_CNTL1 (0x001 << 2)
#define HHI_MIPI_CNTL2 (0x002 << 2)
#define HHI_MEM_PD_REG0 (0x040 << 2)
#define HHI_VPU_MEM_PD_REG0 (0x041 << 2)
#define HHI_VPU_MEM_PD_REG1 (0x042 << 2)
#define HHI_VIID_CLK_DIV (0x04a << 2)
#define HHI_VIID_CLK_CNTL (0x04b << 2)
#define HHI_VPU_MEM_PD_REG2 (0x04d << 2)
#define HHI_VID_CLK_DIV (0x059 << 2)
#define HHI_VID_CLK_CNTL2 (0x065 << 2)
#define HHI_VID_PLL_CLK_DIV (0x068 << 2)
#define HHI_VPU_CLK_CNTL (0x06f << 2)
#define HHI_VAPBCLK_CNTL (0x07d << 2)
#define HHI_VPU_CLKB_CNTL (0x083 << 2)
#define HHI_VDIN_MEAS_CLK_CNTL (0x094 << 2)
#define HHI_MIPIDSI_PHY_CLK_CNTL (0x095 << 2)
#define HHI_HDMI_PLL_CNTL0 (0x0c8 << 2)
#define HHI_HDMI_PLL_CNTL1 (0x0c9 << 2)
#define HHI_HDMI_PLL_CNTL2 (0x0ca << 2)
#define HHI_HDMI_PLL_CNTL3 (0x0cb << 2)
#define HHI_HDMI_PLL_CNTL4 (0x0cc << 2)
#define HHI_HDMI_PLL_CNTL5 (0x0cd << 2)
#define HHI_HDMI_PLL_CNTL6 (0x0ce << 2)

#define ENCL_VIDEO_FILT_CTRL (0x1cc2 << 2)
#define ENCL_VIDEO_MAX_PXCNT (0x1cb0 << 2)
#define ENCL_VIDEO_HAVON_END (0x1cb1 << 2)
#define ENCL_VIDEO_HAVON_BEGIN (0x1cb2 << 2)
#define ENCL_VIDEO_VAVON_ELINE (0x1cb3 << 2)
#define ENCL_VIDEO_VAVON_BLINE (0x1cb4 << 2)
#define ENCL_VIDEO_HSO_BEGIN (0x1cb5 << 2)
#define ENCL_VIDEO_HSO_END (0x1cb6 << 2)
#define ENCL_VIDEO_VSO_BEGIN (0x1cb7 << 2)
#define ENCL_VIDEO_VSO_END (0x1cb8 << 2)
#define ENCL_VIDEO_VSO_BLINE (0x1cb9 << 2)
#define ENCL_VIDEO_VSO_ELINE (0x1cba << 2)
#define ENCL_VIDEO_MAX_LNCNT (0x1cbb << 2)
#define ENCL_VIDEO_MODE (0x1ca7 << 2)
#define ENCL_VIDEO_MODE_ADV (0x1ca8 << 2)
#define ENCL_VIDEO_RGBIN_CTRL (0x1cc7 << 2)
#define ENCL_VIDEO_EN (0x1ca0 << 2)

#define L_DE_HE_ADDR (0x1452 << 2)
#define L_DE_HS_ADDR (0x1451 << 2)
#define L_DE_VE_ADDR (0x1454 << 2)
#define L_DE_VS_ADDR (0x1453 << 2)
#define L_DITH_CNTL_ADDR (0x1408 << 2)
#define L_HSYNC_HE_ADDR (0x1456 << 2)
#define L_HSYNC_HS_ADDR (0x1455 << 2)
#define L_HSYNC_VS_ADDR (0x1457 << 2)
#define L_HSYNC_VE_ADDR (0x1458 << 2)
#define L_VSYNC_HS_ADDR (0x1459 << 2)
#define L_VSYNC_HE_ADDR (0x145a << 2)
#define L_VSYNC_VS_ADDR (0x145b << 2)
#define L_VSYNC_VE_ADDR (0x145c << 2)
#define L_OEH_HS_ADDR (0x1418 << 2)
#define L_OEH_HE_ADDR (0x1419 << 2)
#define L_OEH_VS_ADDR (0x141a << 2)
#define L_OEH_VE_ADDR (0x141b << 2)
#define L_OEV1_HS_ADDR (0x142f << 2)
#define L_OEV1_HE_ADDR (0x1430 << 2)
#define L_OEV1_VS_ADDR (0x1431 << 2)
#define L_OEV1_VE_ADDR (0x1432 << 2)
#define L_RGB_BASE_ADDR (0x1405 << 2)
#define L_RGB_COEFF_ADDR (0x1406 << 2)
#define L_STH1_HS_ADDR (0x1410 << 2)
#define L_STH1_HE_ADDR (0x1411 << 2)
#define L_STH1_VS_ADDR (0x1412 << 2)
#define L_STH1_VE_ADDR (0x1413 << 2)
#define L_TCON_MISC_SEL_ADDR (0x1441 << 2)
#define L_STV1_HS_ADDR (0x1427 << 2)
#define L_STV1_HE_ADDR (0x1428 << 2)
#define L_STV1_VS_ADDR (0x1429 << 2)
#define L_STV1_VE_ADDR (0x142a << 2)
#define L_INV_CNT_ADDR (0x1440 << 2)

#define VPP_MISC (0x1d26 << 2)
#define VPP_OUT_SATURATE (1 << 0)

// HHI_MIPI_CNTL0 Register Bit Def
#define MIPI_CNTL0_CMN_REF_GEN_CTRL(x) (x << 26)
#define MIPI_CNTL0_VREF_SEL(x) (x << 25)
#define VREF_SEL_VR 0
#define VREF_SEL_VBG 1
#define MIPI_CNTL0_LREF_SEL(x) (x << 24)
#define LREF_SEL_L_ROUT 0
#define LREF_SEL_LBG 1
#define MIPI_CNTL0_LBG_EN (1 << 23)
#define MIPI_CNTL0_VR_TRIM_CNTL(x) (x << 16)
#define MIPI_CNTL0_VR_GEN_FROM_LGB_EN (1 << 3)
#define MIPI_CNTL0_VR_GEN_BY_AVDD18_EN (1 << 2)

// HHI_MIPI_CNTL1 Register Bit Def
#define MIPI_CNTL1_DSI_VBG_EN (1 << 16)
#define MIPI_CNTL1_CTL (0x2e << 0)

// HHI_MIPI_CNTL2 Register Bit Def
#define MIPI_CNTL2_DEFAULT_VAL (0x2680fc5a)  // 4-lane DSI LINK

// HHI_VIID_CLK_DIV Register Bit Def
#define DAC0_CLK_SEL (28)
#define DAC1_CLK_SEL (24)
#define DAC2_CLK_SEL (20)
#define VCLK2_XD_RST (17)
#define VCLK2_XD_EN (16)
#define ENCL_CLK_SEL (12)
#define VCLK2_XD (0)

// HHI_VIID_CLK_CNTL Register Bit Def
#define VCLK2_EN (19)
#define VCLK2_CLK_IN_SEL (16)
#define VCLK2_SOFT_RST (15)
#define VCLK2_DIV12_EN (4)
#define VCLK2_DIV6_EN (3)
#define VCLK2_DIV4_EN (2)
#define VCLK2_DIV2_EN (1)
#define VCLK2_DIV1_EN (0)

// HHI_VIID_DIVIDER_CNTL Register Bit Def
#define DIV_CLK_IN_EN (16)
#define DIV_CLK_SEL (15)
#define DIV_POST_TCNT (12)
#define DIV_LVDS_CLK_EN (11)
#define DIV_LVDS_DIV2 (10)
#define DIV_POST_SEL (8)
#define DIV_POST_SOFT_RST (7)
#define DIV_PRE_SEL (4)
#define DIV_PRE_SOFT_RST (3)
#define DIV_POST_RST (1)
#define DIV_PRE_RST (0)

// HHI_VID_CLK_DIV Register Bit Def
#define ENCI_CLK_SEL (28)
#define ENCP_CLK_SEL (24)
#define ENCT_CLK_SEL (20)
#define VCLK_XD_RST (17)
#define VCLK_XD_EN (16)
#define ENCL_CLK_SEL (12)
#define VCLK_XD1 (8)
#define VCLK_XD0 (0)

// HHI_VID_CLK_CNTL2 Register Bit Def
#define HDMI_TX_PIXEL_GATE_VCLK (5)
#define VDAC_GATE_VCLK (4)
#define ENCL_GATE_VCLK (3)
#define ENCP_GATE_VCLK (2)
#define ENCT_GATE_VCLK (1)
#define ENCI_GATE_VCLK (0)

// HHI_HDMI_PHY_CNTL Register Bit Def
#define LCD_PLL_LOCK_HPLL_G12A (31)
#define LCD_PLL_EN_HPLL_G12A (28)
#define LCD_PLL_RST_HPLL_G12A (29)
#define LCD_PLL_OUT_GATE_CTRL_G12A (25)
#define LCD_PLL_OD3_HPLL_G12A (20)
#define LCD_PLL_OD2_HPLL_G12A (18)
#define LCD_PLL_OD1_HPLL_G12A (16)
#define LCD_PLL_N_HPLL_G12A (10)
#define LCD_PLL_M_HPLL_G12A (0)

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_ASTRO_DISPLAY_HHI_REGS_H_
