// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "astro-display.h"

void dump_dsi_host(astro_display_t* display) {
    DISP_INFO("%s: DUMPING DSI HOST REGS\n", __func__);
    DISP_INFO("DW_DSI_VERSION = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_VERSION));
    DISP_INFO("DW_DSI_PWR_UP = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_PWR_UP));
    DISP_INFO("DW_DSI_CLKMGR_CFG = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_CLKMGR_CFG));
    DISP_INFO("DW_DSI_DPI_VCID = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_DPI_VCID));
    DISP_INFO("DW_DSI_DPI_COLOR_CODING = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_DPI_COLOR_CODING));
    DISP_INFO("DW_DSI_DPI_CFG_POL = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_DPI_CFG_POL));
    DISP_INFO("DW_DSI_DPI_LP_CMD_TIM = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_DPI_LP_CMD_TIM));
    DISP_INFO("DW_DSI_DBI_VCID = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_DBI_VCID));
    DISP_INFO("DW_DSI_DBI_CFG = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_DBI_CFG));
    DISP_INFO("DW_DSI_DBI_PARTITIONING_EN = 0x%x\n",
              READ32_REG(MIPI_DSI, DW_DSI_DBI_PARTITIONING_EN));
    DISP_INFO("DW_DSI_DBI_CMDSIZE = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_DBI_CMDSIZE));
    DISP_INFO("DW_DSI_PCKHDL_CFG = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_PCKHDL_CFG));
    DISP_INFO("DW_DSI_GEN_VCID = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_GEN_VCID));
    DISP_INFO("DW_DSI_MODE_CFG = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_MODE_CFG));
    DISP_INFO("DW_DSI_VID_MODE_CFG = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_VID_MODE_CFG));
    DISP_INFO("DW_DSI_VID_PKT_SIZE = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_VID_PKT_SIZE));
    DISP_INFO("DW_DSI_VID_NUM_CHUNKS = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_VID_NUM_CHUNKS));
    DISP_INFO("DW_DSI_VID_NULL_SIZE = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_VID_NULL_SIZE));
    DISP_INFO("DW_DSI_VID_HSA_TIME = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_VID_HSA_TIME));
    DISP_INFO("DW_DSI_VID_HBP_TIME = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_VID_HBP_TIME));
    DISP_INFO("DW_DSI_VID_HLINE_TIME = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_VID_HLINE_TIME));
    DISP_INFO("DW_DSI_VID_VSA_LINES = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_VID_VSA_LINES));
    DISP_INFO("DW_DSI_VID_VBP_LINES = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_VID_VBP_LINES));
    DISP_INFO("DW_DSI_VID_VFP_LINES = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_VID_VFP_LINES));
    DISP_INFO("DW_DSI_VID_VACTIVE_LINES = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_VID_VACTIVE_LINES));
    DISP_INFO("DW_DSI_EDPI_CMD_SIZE = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_EDPI_CMD_SIZE));
    DISP_INFO("DW_DSI_CMD_MODE_CFG = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_CMD_MODE_CFG));
    DISP_INFO("DW_DSI_GEN_HDR = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_GEN_HDR));
    DISP_INFO("DW_DSI_GEN_PLD_DATA = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_GEN_PLD_DATA));
    DISP_INFO("DW_DSI_CMD_PKT_STATUS = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_CMD_PKT_STATUS));
    DISP_INFO("DW_DSI_TO_CNT_CFG = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_TO_CNT_CFG));
    DISP_INFO("DW_DSI_HS_RD_TO_CNT = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_HS_RD_TO_CNT));
    DISP_INFO("DW_DSI_LP_RD_TO_CNT = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_LP_RD_TO_CNT));
    DISP_INFO("DW_DSI_HS_WR_TO_CNT = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_HS_WR_TO_CNT));
    DISP_INFO("DW_DSI_LP_WR_TO_CNT = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_LP_WR_TO_CNT));
    DISP_INFO("DW_DSI_BTA_TO_CNT = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_BTA_TO_CNT));
    DISP_INFO("DW_DSI_SDF_3D = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_SDF_3D));
    DISP_INFO("DW_DSI_LPCLK_CTRL = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_LPCLK_CTRL));
    DISP_INFO("DW_DSI_PHY_TMR_LPCLK_CFG = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_PHY_TMR_LPCLK_CFG));
    DISP_INFO("DW_DSI_PHY_TMR_CFG = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_PHY_TMR_CFG));
    DISP_INFO("DW_DSI_PHY_RSTZ = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_PHY_RSTZ));
    DISP_INFO("DW_DSI_PHY_IF_CFG = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_PHY_IF_CFG));
    DISP_INFO("DW_DSI_PHY_ULPS_CTRL = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_PHY_ULPS_CTRL));
    DISP_INFO("DW_DSI_PHY_TX_TRIGGERS = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_PHY_TX_TRIGGERS));
    DISP_INFO("DW_DSI_PHY_STATUS = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_PHY_STATUS));
    DISP_INFO("DW_DSI_PHY_TST_CTRL0 = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_PHY_TST_CTRL0));
    DISP_INFO("DW_DSI_PHY_TST_CTRL1 = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_PHY_TST_CTRL1));
    DISP_INFO("DW_DSI_INT_ST0 = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_INT_ST0));
    DISP_INFO("DW_DSI_INT_ST1 = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_INT_ST1));
    DISP_INFO("DW_DSI_INT_MSK0 = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_INT_MSK0));
    DISP_INFO("DW_DSI_INT_MSK1 = 0x%x\n", READ32_REG(MIPI_DSI, DW_DSI_INT_MSK1));

    DISP_INFO("MIPI_DSI_TOP_SW_RESET = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_SW_RESET));
    DISP_INFO("MIPI_DSI_TOP_CLK_CNTL = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_CLK_CNTL));
    DISP_INFO("MIPI_DSI_TOP_CNTL = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_CNTL));
    DISP_INFO("MIPI_DSI_TOP_SUSPEND_CNTL = 0x%x\n",
              READ32_REG(MIPI_DSI, MIPI_DSI_TOP_SUSPEND_CNTL));
    DISP_INFO("MIPI_DSI_TOP_SUSPEND_LINE = 0x%x\n",
              READ32_REG(MIPI_DSI, MIPI_DSI_TOP_SUSPEND_LINE));
    DISP_INFO("MIPI_DSI_TOP_SUSPEND_PIX = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_SUSPEND_PIX));
    DISP_INFO("MIPI_DSI_TOP_MEAS_CNTL = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_MEAS_CNTL));
    DISP_INFO("MIPI_DSI_TOP_STAT = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_STAT));
    DISP_INFO("MIPI_DSI_TOP_MEAS_STAT_TE0 = 0x%x\n",
              READ32_REG(MIPI_DSI, MIPI_DSI_TOP_MEAS_STAT_TE0));
    DISP_INFO("MIPI_DSI_TOP_MEAS_STAT_TE1 = 0x%x\n",
              READ32_REG(MIPI_DSI, MIPI_DSI_TOP_MEAS_STAT_TE1));
    DISP_INFO("MIPI_DSI_TOP_MEAS_STAT_VS0 = 0x%x\n",
              READ32_REG(MIPI_DSI, MIPI_DSI_TOP_MEAS_STAT_VS0));
    DISP_INFO("MIPI_DSI_TOP_MEAS_STAT_VS1 = 0x%x\n",
              READ32_REG(MIPI_DSI, MIPI_DSI_TOP_MEAS_STAT_VS1));
    DISP_INFO("MIPI_DSI_TOP_INTR_CNTL_STAT = 0x%x\n",
              READ32_REG(MIPI_DSI, MIPI_DSI_TOP_INTR_CNTL_STAT));
    DISP_INFO("MIPI_DSI_TOP_MEM_PD = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_MEM_PD));

}

void dump_dsi_phy(astro_display_t* display) {
    DISP_INFO("%s: DUMPING PHY REGS\n", __func__);

    DISP_INFO("MIPI_DSI_PHY_CTRL = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_PHY_CTRL));
    DISP_INFO("MIPI_DSI_CHAN_CTRL = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_CHAN_CTRL));
    DISP_INFO("MIPI_DSI_CHAN_STS = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_CHAN_STS));
    DISP_INFO("MIPI_DSI_CLK_TIM = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_CLK_TIM));
    DISP_INFO("MIPI_DSI_HS_TIM = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_HS_TIM));
    DISP_INFO("MIPI_DSI_LP_TIM = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_LP_TIM));
    DISP_INFO("MIPI_DSI_ANA_UP_TIM = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_ANA_UP_TIM));
    DISP_INFO("MIPI_DSI_INIT_TIM = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_INIT_TIM));
    DISP_INFO("MIPI_DSI_WAKEUP_TIM = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_WAKEUP_TIM));
    DISP_INFO("MIPI_DSI_LPOK_TIM = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_LPOK_TIM));
    DISP_INFO("MIPI_DSI_LP_WCHDOG = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_LP_WCHDOG));
    DISP_INFO("MIPI_DSI_ANA_CTRL = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_ANA_CTRL));
    DISP_INFO("MIPI_DSI_CLK_TIM1 = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_CLK_TIM1));
    DISP_INFO("MIPI_DSI_TURN_WCHDOG = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_TURN_WCHDOG));
    DISP_INFO("MIPI_DSI_ULPS_CHECK = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_ULPS_CHECK));
    DISP_INFO("MIPI_DSI_TEST_CTRL0 = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_TEST_CTRL0));
    DISP_INFO("MIPI_DSI_TEST_CTRL1 = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_TEST_CTRL1));

    DISP_INFO("\n");

}

void dump_display_info(astro_display_t* display) {
    DISP_INFO("#############################\n");
    DISP_INFO("Dumping pll_cfg structure:\n");
    DISP_INFO("#############################\n");
    DISP_INFO("fin = 0x%x (%u)\n", display->pll_cfg.fin,display->pll_cfg.fin);
    DISP_INFO("fout = 0x%x (%u)\n", display->pll_cfg.fout,display->pll_cfg.fout);
    DISP_INFO("pll_m = 0x%x (%u)\n", display->pll_cfg.pll_m,display->pll_cfg.pll_m);
    DISP_INFO("pll_n = 0x%x (%u)\n", display->pll_cfg.pll_n,display->pll_cfg.pll_n);
    DISP_INFO("pll_fvco = 0x%x (%u)\n", display->pll_cfg.pll_fvco,display->pll_cfg.pll_fvco);
    DISP_INFO("pll_od1_sel = 0x%x (%u)\n", display->pll_cfg.pll_od1_sel,
              display->pll_cfg.pll_od1_sel);
    DISP_INFO("pll_od2_sel = 0x%x (%u)\n", display->pll_cfg.pll_od2_sel,
              display->pll_cfg.pll_od2_sel);
    DISP_INFO("pll_od3_sel = 0x%x (%u)\n", display->pll_cfg.pll_od3_sel,
              display->pll_cfg.pll_od3_sel);
    DISP_INFO("pll_frac = 0x%x (%u)\n", display->pll_cfg.pll_frac,display->pll_cfg.pll_frac);
    DISP_INFO("pll_fout = 0x%x (%u)\n", display->pll_cfg.pll_fout,display->pll_cfg.pll_fout);

    DISP_INFO("#############################\n");
    DISP_INFO("Dumping disp_setting structure:\n");
    DISP_INFO("#############################\n");
    DISP_INFO("bitrate = 0x%x (%u)\n", display->pll_cfg.bitrate,
              display->pll_cfg.bitrate);
    DISP_INFO("h_active = 0x%x (%u)\n", display->disp_setting.h_active,
              display->disp_setting.h_active);
    DISP_INFO("v_active = 0x%x (%u)\n", display->disp_setting.v_active,
              display->disp_setting.v_active);
    DISP_INFO("h_period = 0x%x (%u)\n", display->disp_setting.h_period,
              display->disp_setting.h_period);
    DISP_INFO("v_period = 0x%x (%u)\n", display->disp_setting.v_period,
              display->disp_setting.v_period);
    DISP_INFO("hsync_width = 0x%x (%u)\n", display->disp_setting.hsync_width,
              display->disp_setting.hsync_width);
    DISP_INFO("hsync_bp = 0x%x (%u)\n", display->disp_setting.hsync_bp,
              display->disp_setting.hsync_bp);
    DISP_INFO("hsync_pol = 0x%x (%u)\n", display->disp_setting.hsync_pol,
              display->disp_setting.hsync_pol);
    DISP_INFO("vsync_width = 0x%x (%u)\n", display->disp_setting.vsync_width,
              display->disp_setting.vsync_width);
    DISP_INFO("vsync_bp = 0x%x (%u)\n", display->disp_setting.vsync_bp,
              display->disp_setting.vsync_bp);
    DISP_INFO("vsync_pol = 0x%x (%u)\n", display->disp_setting.vsync_pol,
              display->disp_setting.vsync_pol);
    DISP_INFO("lcd_clock = 0x%x (%u)\n", display->disp_setting.lcd_clock,
              display->disp_setting.lcd_clock);
    DISP_INFO("lane_num = 0x%x (%u)\n", display->disp_setting.lane_num,
              display->disp_setting.lane_num);
    DISP_INFO("bit_rate_max = 0x%x (%u)\n", display->disp_setting.bit_rate_max,
              display->disp_setting.bit_rate_max);
    DISP_INFO("clock_factor = 0x%x (%u)\n", display->disp_setting.clock_factor,
              display->disp_setting.clock_factor);

    DISP_INFO("#############################\n");
    DISP_INFO("Dumping lcd_timing structure:\n");
    DISP_INFO("#############################\n");
    DISP_INFO("vid_pixel_on = 0x%x (%u)\n", display->lcd_timing.vid_pixel_on,
              display->lcd_timing.vid_pixel_on);
    DISP_INFO("vid_line_on = 0x%x (%u)\n", display->lcd_timing.vid_line_on,
              display->lcd_timing.vid_line_on);
    DISP_INFO("de_hs_addr = 0x%x (%u)\n", display->lcd_timing.de_hs_addr,
              display->lcd_timing.de_hs_addr);
    DISP_INFO("de_he_addr = 0x%x (%u)\n", display->lcd_timing.de_he_addr,
              display->lcd_timing.de_he_addr);
    DISP_INFO("de_vs_addr = 0x%x (%u)\n", display->lcd_timing.de_vs_addr,
              display->lcd_timing.de_vs_addr);
    DISP_INFO("de_ve_addr = 0x%x (%u)\n", display->lcd_timing.de_ve_addr,
              display->lcd_timing.de_ve_addr);
    DISP_INFO("hs_hs_addr = 0x%x (%u)\n", display->lcd_timing.hs_hs_addr,
              display->lcd_timing.hs_hs_addr);
    DISP_INFO("hs_he_addr = 0x%x (%u)\n", display->lcd_timing.hs_he_addr,
              display->lcd_timing.hs_he_addr);
    DISP_INFO("hs_vs_addr = 0x%x (%u)\n", display->lcd_timing.hs_vs_addr,
              display->lcd_timing.hs_vs_addr);
    DISP_INFO("hs_ve_addr = 0x%x (%u)\n", display->lcd_timing.hs_ve_addr,
              display->lcd_timing.hs_ve_addr);
    DISP_INFO("vs_hs_addr = 0x%x (%u)\n", display->lcd_timing.vs_hs_addr,
              display->lcd_timing.vs_hs_addr);
    DISP_INFO("vs_he_addr = 0x%x (%u)\n", display->lcd_timing.vs_he_addr,
              display->lcd_timing.vs_he_addr);
    DISP_INFO("vs_vs_addr = 0x%x (%u)\n", display->lcd_timing.vs_vs_addr,
              display->lcd_timing.vs_vs_addr);
    DISP_INFO("vs_ve_addr = 0x%x (%u)\n", display->lcd_timing.vs_ve_addr,
              display->lcd_timing.vs_ve_addr);

    DISP_INFO("#############################\n");
    DISP_INFO("Dumping dsi_phy_cfg structure:\n");
    DISP_INFO("#############################\n");
    DISP_INFO("lp_tesc = 0x%x (%u)\n", display->dsi_phy_cfg.lp_tesc,
              display->dsi_phy_cfg.lp_tesc);
    DISP_INFO("lp_lpx = 0x%x (%u)\n", display->dsi_phy_cfg.lp_lpx,
              display->dsi_phy_cfg.lp_lpx);
    DISP_INFO("lp_ta_sure = 0x%x (%u)\n", display->dsi_phy_cfg.lp_ta_sure,
              display->dsi_phy_cfg.lp_ta_sure);
    DISP_INFO("lp_ta_go = 0x%x (%u)\n", display->dsi_phy_cfg.lp_ta_go,
              display->dsi_phy_cfg.lp_ta_go);
    DISP_INFO("lp_ta_get = 0x%x (%u)\n", display->dsi_phy_cfg.lp_ta_get,
              display->dsi_phy_cfg.lp_ta_get);
    DISP_INFO("hs_exit = 0x%x (%u)\n", display->dsi_phy_cfg.hs_exit,
              display->dsi_phy_cfg.hs_exit);
    DISP_INFO("hs_trail = 0x%x (%u)\n", display->dsi_phy_cfg.hs_trail,
              display->dsi_phy_cfg.hs_trail);
    DISP_INFO("hs_zero = 0x%x (%u)\n", display->dsi_phy_cfg.hs_zero,
              display->dsi_phy_cfg.hs_zero);
    DISP_INFO("hs_prepare = 0x%x (%u)\n", display->dsi_phy_cfg.hs_prepare,
              display->dsi_phy_cfg.hs_prepare);
    DISP_INFO("clk_trail = 0x%x (%u)\n", display->dsi_phy_cfg.clk_trail,
              display->dsi_phy_cfg.clk_trail);
    DISP_INFO("clk_post = 0x%x (%u)\n", display->dsi_phy_cfg.clk_post,
              display->dsi_phy_cfg.clk_post);
    DISP_INFO("clk_zero = 0x%x (%u)\n", display->dsi_phy_cfg.clk_zero,
              display->dsi_phy_cfg.clk_zero);
    DISP_INFO("clk_prepare = 0x%x (%u)\n", display->dsi_phy_cfg.clk_prepare,
              display->dsi_phy_cfg.clk_prepare);
    DISP_INFO("clk_pre = 0x%x (%u)\n", display->dsi_phy_cfg.clk_pre,
              display->dsi_phy_cfg.clk_pre);
    DISP_INFO("init = 0x%x (%u)\n", display->dsi_phy_cfg.init,
              display->dsi_phy_cfg.init);
    DISP_INFO("wakeup = 0x%x (%u)\n", display->dsi_phy_cfg.wakeup,
              display->dsi_phy_cfg.wakeup);
}
