// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "astro-display.h"

#define NS_TO_LANEBYTE(x) ((x + lanebytetime - 1) / (lanebytetime))

zx_status_t mipi_dsi_phy_cfg_load(astro_display_t* display, uint32_t bitrate) {
    if (display == NULL) {
        DISP_ERROR("Uninitialized memory detected!\n");
        return ZX_ERR_INVALID_ARGS;
    }

    // According to MIPI -PHY Spec, we need to define Unit Interval (UI).
    // This UI is defined as the time it takes to send a bit (i.e. bitrate)
    // The x100 is to ensure the ui is not rounded too much (i.e. 2.56 --> 256)
    // However, since we have introduced x100, we need to make sure we include x100
    // to all the PHY timings that are in ns units.
    uint32_t ui = (1 * 1000 * 1000 * 100) / (bitrate / 1000);

    // Calculate values will be rounded by the lanebyteclk
    uint32_t lanebytetime = ui * 8;

    // lp_tesc:TX Excape Clock Division factor (from linebyteclk). Round up to units of ui
    display->dsi_phy_cfg.lp_tesc = NS_TO_LANEBYTE(DPHY_TIME_LP_TESC) & 0xff;

    // lp_lpx: Transmit length of any LP state period
    display->dsi_phy_cfg.lp_lpx = NS_TO_LANEBYTE(DPHY_TIME_LP_LPX) & 0xff;

    // lp_ta_sure
    display->dsi_phy_cfg.lp_ta_sure = NS_TO_LANEBYTE(DPHY_TIME_LP_TA_SURE) & 0xff;

    // lp_ta_go
    display->dsi_phy_cfg.lp_ta_go = NS_TO_LANEBYTE(DPHY_TIME_LP_TA_GO) & 0xff;

    // lp_ta_get
    display->dsi_phy_cfg.lp_ta_get = NS_TO_LANEBYTE(DPHY_TIME_LP_TA_GET) & 0xff;

    // hs_exit
    display->dsi_phy_cfg.hs_exit = NS_TO_LANEBYTE(DPHY_TIME_HS_EXIT) & 0xff;

    // clk-_prepare
    display->dsi_phy_cfg.clk_prepare = NS_TO_LANEBYTE(DPHY_TIME_CLK_PREPARE) & 0xff;

    // clk_zero
    display->dsi_phy_cfg.clk_zero = NS_TO_LANEBYTE(DPHY_TIME_CLK_ZERO(ui)) & 0xff;

    // clk_pre
    display->dsi_phy_cfg.clk_pre = NS_TO_LANEBYTE(DPHY_TIME_CLK_PRE(ui)) & 0xff;

    // init
    display->dsi_phy_cfg.init = NS_TO_LANEBYTE(DPHY_TIME_INIT) & 0xff;

    // wakeup
    display->dsi_phy_cfg.wakeup = NS_TO_LANEBYTE(DPHY_TIME_WAKEUP) & 0xff;

    // clk_trail
    display->dsi_phy_cfg.clk_trail = NS_TO_LANEBYTE(DPHY_TIME_CLK_TRAIL) & 0xff;

    // clk_post
    display->dsi_phy_cfg.clk_post = NS_TO_LANEBYTE(DPHY_TIME_CLK_POST(ui)) & 0xff;

    // hs_trail
    display->dsi_phy_cfg.hs_trail = NS_TO_LANEBYTE(DPHY_TIME_HS_TRAIL(ui)) & 0xff;

    // hs_prepare
    display->dsi_phy_cfg.hs_prepare = NS_TO_LANEBYTE(DPHY_TIME_HS_PREPARE(ui)) & 0xff;

    // hs_zero
    display->dsi_phy_cfg.hs_zero = NS_TO_LANEBYTE(DPHY_TIME_HS_ZERO(ui)) & 0xff;

    // Ensure both clk-trail and hs-trail do not exceed Teot
    uint32_t t_req_max = NS_TO_LANEBYTE(DPHY_TIME_EOT(ui)) & 0xff;
    if ((display->dsi_phy_cfg.clk_trail > t_req_max) ||
        (display->dsi_phy_cfg.hs_trail > t_req_max)) {
        DISP_ERROR("Invalid clk-trail and/or hs-trail exceed Teot!\n");
        DISP_ERROR("clk-trail = 0x%02x, hs-trail =  0x%02x, Teot = 0x%02x\n",
                   display->dsi_phy_cfg.clk_trail, display->dsi_phy_cfg.hs_trail, t_req_max );
        return ZX_ERR_OUT_OF_RANGE;
    }

    DISP_SPEW( "lp_tesc     = 0x%02x\n"
                "lp_lpx      = 0x%02x\n"
                "lp_ta_sure  = 0x%02x\n"
                "lp_ta_go    = 0x%02x\n"
                "lp_ta_get   = 0x%02x\n"
                "hs_exit     = 0x%02x\n"
                "hs_trail    = 0x%02x\n"
                "hs_zero     = 0x%02x\n"
                "hs_prepare  = 0x%02x\n"
                "clk_trail   = 0x%02x\n"
                "clk_post    = 0x%02x\n"
                "clk_zero    = 0x%02x\n"
                "clk_prepare = 0x%02x\n"
                "clk_pre     = 0x%02x\n"
                "init        = 0x%02x\n"
                "wakeup      = 0x%02x\n\n",
                display->dsi_phy_cfg.lp_tesc,
                display->dsi_phy_cfg.lp_lpx,
                display->dsi_phy_cfg.lp_ta_sure,
                display->dsi_phy_cfg.lp_ta_go,
                display->dsi_phy_cfg.lp_ta_get,
                display->dsi_phy_cfg.hs_exit,
                display->dsi_phy_cfg.hs_trail,
                display->dsi_phy_cfg.hs_zero,
                display->dsi_phy_cfg.hs_prepare,
                display->dsi_phy_cfg.clk_trail,
                display->dsi_phy_cfg.clk_post,
                display->dsi_phy_cfg.clk_zero,
                display->dsi_phy_cfg.clk_prepare,
                display->dsi_phy_cfg.clk_pre,
                display->dsi_phy_cfg.init,
                display->dsi_phy_cfg.wakeup);
    return ZX_OK;
}

static void aml_dsi_phy_init(astro_display_t* display, uint32_t lane_num)
{
    // enable phy clock.
    WRITE32_REG(DSI_PHY, MIPI_DSI_PHY_CTRL, PHY_CTRL_TXDDRCLK_EN |
                PHY_CTRL_DDRCLKPATH_EN | PHY_CTRL_CLK_DIV_COUNTER | PHY_CTRL_CLK_DIV_EN |
                PHY_CTRL_BYTECLK_EN);

    // Toggle PHY CTRL RST
    SET_BIT32(DSI_PHY, MIPI_DSI_PHY_CTRL, 1, PHY_CTRL_RST_START, PHY_CTRL_RST_BITS);
    SET_BIT32(DSI_PHY, MIPI_DSI_PHY_CTRL, 0, PHY_CTRL_RST_START, PHY_CTRL_RST_BITS);

    WRITE32_REG(DSI_PHY, MIPI_DSI_CLK_TIM,
                (display->dsi_phy_cfg.clk_trail | (display->dsi_phy_cfg.clk_post << 8) |
                (display->dsi_phy_cfg.clk_zero << 16) |
                (display->dsi_phy_cfg.clk_prepare << 24)));

    WRITE32_REG(DSI_PHY, MIPI_DSI_CLK_TIM1, display->dsi_phy_cfg.clk_pre);

    WRITE32_REG(DSI_PHY, MIPI_DSI_HS_TIM,
                (display->dsi_phy_cfg.hs_exit | (display->dsi_phy_cfg.hs_trail << 8) |
                (display->dsi_phy_cfg.hs_zero << 16) |
                (display->dsi_phy_cfg.hs_prepare << 24)));

    WRITE32_REG(DSI_PHY, MIPI_DSI_LP_TIM,
                (display->dsi_phy_cfg.lp_lpx | (display->dsi_phy_cfg.lp_ta_sure << 8) |
                (display->dsi_phy_cfg.lp_ta_go << 16) | (display->dsi_phy_cfg.lp_ta_get << 24)));

    WRITE32_REG(DSI_PHY, MIPI_DSI_ANA_UP_TIM, ANA_UP_TIME);
    WRITE32_REG(DSI_PHY, MIPI_DSI_INIT_TIM, display->dsi_phy_cfg.init);
    WRITE32_REG(DSI_PHY, MIPI_DSI_WAKEUP_TIM, display->dsi_phy_cfg.wakeup);
    WRITE32_REG(DSI_PHY, MIPI_DSI_LPOK_TIM,  LPOK_TIME);
    WRITE32_REG(DSI_PHY, MIPI_DSI_ULPS_CHECK,  ULPS_CHECK_TIME);
    WRITE32_REG(DSI_PHY, MIPI_DSI_LP_WCHDOG,  LP_WCHDOG_TIME);
    WRITE32_REG(DSI_PHY, MIPI_DSI_TURN_WCHDOG,  TURN_WCHDOG_TIME);

    WRITE32_REG(DSI_PHY, MIPI_DSI_CHAN_CTRL, 0);
}


// This function checks two things in order to decide whether the PHY is
// ready or not. LOCK Bit and StopStateClk bit. According to spec, once these
// are set, PHY has completed initialization
static zx_status_t aml_dsi_waitfor_phy_ready(astro_display_t* display)
{
    int timeout = DPHY_TIMEOUT;
    while ((GET_BIT32(MIPI_DSI, DW_DSI_PHY_STATUS, PHY_STATUS_PHY_LOCK, 1) == 0) &&
           timeout--) {
        usleep(6);
    }
    if (timeout <= 0) {
        DISP_ERROR("Timeout! D-PHY did not lock\n");
        return ZX_ERR_TIMED_OUT;
    }

    timeout = DPHY_TIMEOUT;
    while ((GET_BIT32(MIPI_DSI, DW_DSI_PHY_STATUS, PHY_STATUS_PHY_STOPSTATECLKLANE, 1) == 0) &&
           timeout--) {
        usleep(6);
    }
    if (timeout <= 0) {
        DISP_ERROR("Timeout! D-PHY StopStateClk not set\n");
        return ZX_ERR_TIMED_OUT;
    }
    return  ZX_OK;
}

zx_status_t mipi_dsi_phy_init(astro_display_t* display)
{
    if (display == NULL) {
        DISP_ERROR("Uninitialized memory detected!\n");
        return ZX_ERR_INVALID_ARGS;
    }

    // Power up DSI
    WRITE32_REG(MIPI_DSI, DW_DSI_PWR_UP, PWR_UP_ON);

    // Setup Parameters of DPHY
    // Below we are sending test code 0x44 with parameter 0x74. This means
    // we are setting up the phy to operate in 1050-1099 Mbps mode
    // TODO: Find out why 0x74 was selected
    WRITE32_REG(MIPI_DSI, DW_DSI_PHY_TST_CTRL1, 0x00010044);
    WRITE32_REG(MIPI_DSI, DW_DSI_PHY_TST_CTRL0, 0x2);
    WRITE32_REG(MIPI_DSI, DW_DSI_PHY_TST_CTRL0, 0x0);
    WRITE32_REG(MIPI_DSI, DW_DSI_PHY_TST_CTRL1, 0x00000074);
    WRITE32_REG(MIPI_DSI, DW_DSI_PHY_TST_CTRL0, 0x2);
    WRITE32_REG(MIPI_DSI, DW_DSI_PHY_TST_CTRL0, 0x0);

    // Power up D-PHY
    WRITE32_REG(MIPI_DSI, DW_DSI_PHY_RSTZ, PHY_RSTZ_PWR_UP);

    // Setup PHY Timing parameters
    aml_dsi_phy_init(display, display->disp_setting.lane_num);

    // Wait for PHY to be read
    zx_status_t status;
    if ((status = aml_dsi_waitfor_phy_ready(display)) != ZX_OK) {
        // no need to print additional info.
        return status;
    }

    // Trigger a sync active for esc_clk
    SET_BIT32(DSI_PHY, MIPI_DSI_PHY_CTRL, 1, 1, 1);

    // Startup transfer, default lpclk
    WRITE32_REG(MIPI_DSI, DW_DSI_LPCLK_CTRL, (0x1 << LPCLK_CTRL_AUTOCLKLANE_CTRL) |
                (0x1 << LPCLK_CTRL_TXREQUESTCLKHS));

    return ZX_OK;
}
