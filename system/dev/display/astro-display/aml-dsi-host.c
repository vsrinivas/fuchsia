// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "astro-display.h"

extern zx_status_t mipi_dsi_phy_cfg_load(astro_display_t* display, uint32_t bitrate);
extern zx_status_t mipi_dsi_phy_init(astro_display_t* display);

// This function sets up mipi dsi interface. It includes both DWC and AmLogic blocks
// The DesignWare setup could technically be moved to the dw_mipi_dsi driver. However,
// given the highly configurable nature of this block, we'd have to provide a lot of
// information to the generic driver. Therefore, it's just simpler to configure it here
static zx_status_t aml_dsi_host_init(astro_display_t* display, uint32_t opp) {
    if (display == NULL) {
        DISP_ERROR("Uninitialized memory detected!\n");
        return ZX_ERR_INVALID_ARGS;
    }

    uint32_t lane_num = display->disp_setting.lane_num;

    // DesignWare DSI Host Setup based on MIPI DSI Host Controller User Guide (Sec 3.1.1)

    // 1. Global configuration: Lane number and PHY stop wait time
    WRITE32_REG(MIPI_DSI, DW_DSI_PHY_IF_CFG, PHY_IF_CFG_STOP_WAIT_TIME |
                PHY_IF_CFG_N_LANES(lane_num));

    // 2.1 Configure virtual channel
    WRITE32_REG(MIPI_DSI, DW_DSI_DPI_VCID, MIPI_DSI_VIRTUAL_CHAN_ID);

    // 2.2, Configure Color format
    WRITE32_REG(MIPI_DSI, DW_DSI_DPI_COLOR_CODING, DPI_COLOR_CODING(SUPPORTED_DPI_FORMAT));

    // Setup relevant TOP_CNTL register -- Undocumented --
    SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_CNTL, SUPPORTED_DPI_FORMAT,
        TOP_CNTL_DPI_CLR_MODE_START, TOP_CNTL_DPI_CLR_MODE_BITS);
    SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_CNTL, SUPPORTED_VENC_DATA_WIDTH,
        TOP_CNTL_IN_CLR_MODE_START, TOP_CNTL_IN_CLR_MODE_BITS);
    SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_CNTL, 0,
        TOP_CNTL_CHROMA_SUBSAMPLE_START, TOP_CNTL_CHROMA_SUBSAMPLE_BITS);

    // 2.3 Configure Signal polarity - Keep as default
    WRITE32_REG(MIPI_DSI, DW_DSI_DPI_CFG_POL, 0);

    if (opp == VIDEO_MODE) {
        // 3.1 Configure low power transitions and video mode type
        WRITE32_REG(MIPI_DSI, DW_DSI_VID_MODE_CFG,VID_MODE_CFG_LP_EN_ALL |
                    (VID_MODE_CFG_VID_MODE_TYPE(SUPPORTED_VIDEO_MODE_TYPE)));

        // Define the max pkt size during Low Power mode
        WRITE32_REG(MIPI_DSI, DW_DSI_DPI_LP_CMD_TIM, LP_CMD_TIM_OUTVACT(LPCMD_PKT_SIZE) |
                    LP_CMD_TIM_INVACT(LPCMD_PKT_SIZE));

        // 3.2   Configure video packet size settings
        WRITE32_REG(MIPI_DSI, DW_DSI_VID_PKT_SIZE, display->disp_setting.h_active);
        // Disable sending vid in chunk since they are ignored by DW host IP in burst mode
        WRITE32_REG(MIPI_DSI, DW_DSI_VID_NUM_CHUNKS, 0);
        WRITE32_REG(MIPI_DSI, DW_DSI_VID_NULL_SIZE, 0);

        // 4 Configure the video relative parameters according to the output type
        WRITE32_REG(MIPI_DSI, DW_DSI_VID_HLINE_TIME, display->disp_setting.h_period);
        WRITE32_REG(MIPI_DSI, DW_DSI_VID_HSA_TIME, display->disp_setting.hsync_width);
        WRITE32_REG(MIPI_DSI, DW_DSI_VID_HBP_TIME, display->disp_setting.hsync_bp);
        WRITE32_REG(MIPI_DSI, DW_DSI_VID_VSA_LINES, display->disp_setting.vsync_width);
        WRITE32_REG(MIPI_DSI, DW_DSI_VID_VBP_LINES, display->disp_setting.vsync_bp);
        WRITE32_REG(MIPI_DSI, DW_DSI_VID_VACTIVE_LINES, display->disp_setting.v_active);
        WRITE32_REG(MIPI_DSI, DW_DSI_VID_VFP_LINES, (display->disp_setting.v_period -
                    display->disp_setting.v_active - display->disp_setting.vsync_bp -
                    display->disp_setting.vsync_width));
    }

    // Internal dividers to divide lanebyteclk for timeout purposes
    WRITE32_REG(MIPI_DSI, DW_DSI_CLKMGR_CFG,
                (CLKMGR_CFG_TO_CLK_DIV(1)) |
                (CLKMGR_CFG_TX_ESC_CLK_DIV(display->dsi_phy_cfg.lp_tesc)));

    // Configure the operation mode (cmd or vid)
    WRITE32_REG(MIPI_DSI, DW_DSI_MODE_CFG, opp);

    // Setup Phy Timers as provided by vendor
    WRITE32_REG(MIPI_DSI, DW_DSI_PHY_TMR_LPCLK_CFG,
                PHY_TMR_LPCLK_CFG_CLKHS_TO_LP(PHY_TMR_LPCLK_CLKHS_TO_LP) |
                PHY_TMR_LPCLK_CFG_CLKLP_TO_HS(PHY_TMR_LPCLK_CLKLP_TO_HS));
    WRITE32_REG(MIPI_DSI, DW_DSI_PHY_TMR_CFG,
                PHY_TMR_CFG_HS_TO_LP(PHY_TMR_HS_TO_LP) |
                PHY_TMR_CFG_LP_TO_HS(PHY_TMR_LP_TO_HS));

    return ZX_OK;
}

// This function enables Amlogic MIPI PHY
static void aml_mipi_phy_enable(astro_display_t* display) {
    WRITE32_REG(HHI, HHI_MIPI_CNTL0, MIPI_CNTL0_CMN_REF_GEN_CTRL(0x29) |
                MIPI_CNTL0_VREF_SEL(VREF_SEL_VR) |
                MIPI_CNTL0_LREF_SEL(LREF_SEL_L_ROUT) |
                MIPI_CNTL0_LBG_EN |
                MIPI_CNTL0_VR_TRIM_CNTL(0x7) |
                MIPI_CNTL0_VR_GEN_FROM_LGB_EN);
    WRITE32_REG(HHI, HHI_MIPI_CNTL1, MIPI_CNTL1_DSI_VBG_EN | MIPI_CNTL1_CTL);
    WRITE32_REG(HHI, HHI_MIPI_CNTL2, MIPI_CNTL2_DEFAULT_VAL); // 4 lane
}

// This function disables Amlogic MIPI PHY
static void aml_mipi_phy_disable(astro_display_t* display) {
    WRITE32_REG(HHI, HHI_MIPI_CNTL0, 0);
    WRITE32_REG(HHI, HHI_MIPI_CNTL1, 0);
    WRITE32_REG(HHI, HHI_MIPI_CNTL2, 0);
}

// This function enables DSI host
zx_status_t aml_dsi_host_on(astro_display_t* display) {
    if (display == NULL) {
        DISP_ERROR("Uninitialized memory detected!\n");
        return ZX_ERR_INVALID_ARGS;
    }

    // Enable MIPI PHY
    aml_mipi_phy_enable(display);

    // Load Phy configuration
    zx_status_t status;
    if ((status = mipi_dsi_phy_cfg_load(display, display->pll_cfg.bitrate)) != ZX_OK) {
        DISP_ERROR("Error during phy config calculations! %d\n", status);
        return status;
    }

    // Enable dwc mipi_dsi_host's clock
    SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_CNTL, 0x3, 4, 2);
    // mipi_dsi_host's reset
    SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_SW_RESET, 0xf, 0, 4);
    // Release mipi_dsi_host's reset
    SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_SW_RESET, 0x0, 0, 4);
    // Enable dwc mipi_dsi_host's clock
    SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_CLK_CNTL, 0x3, 0, 2);

    WRITE32_REG(MIPI_DSI, MIPI_DSI_TOP_MEM_PD, 0);
    usleep(10000);

    // Enable LP transmission in CMD Mode
    WRITE32_REG(MIPI_DSI, DW_DSI_CMD_MODE_CFG,CMD_MODE_CFG_CMD_LP_ALL);

    // Packet header settings - Enable CRC and ECC. BTA will be enabled based on CMD
    WRITE32_REG(MIPI_DSI, DW_DSI_PCKHDL_CFG, PCKHDL_CFG_EN_CRC_ECC);

    // Initialize host in command mode first
    if ((status = aml_dsi_host_init(display, COMMAND_MODE)) != ZX_OK) {
        DISP_ERROR("Error during dsi host init! %d\n", status);
        return status;
    }

    // Initialize mipi dsi D-phy
    if ((status = mipi_dsi_phy_init(display)) != ZX_OK) {
        DISP_ERROR("Error during MIPI D-PHY Initialization! %d\n", status);
        return status;
    }

    // Enable LP Clock
    SET_BIT32(MIPI_DSI, DW_DSI_LPCLK_CTRL, 1, LPCLK_CTRL_AUTOCLKLANE_CTRL, 1);

    // Load LCD Init values while in command mode
    if ((status = lcd_init(display)) != ZX_OK) {
        DISP_ERROR("Error during LCD Initialization! %d\n", status);
        return status;
    }

    // switch to video mode
    if ((status = aml_dsi_host_init(display, VIDEO_MODE)) != ZX_OK) {
        DISP_ERROR("Error during dsi host init! %d\n", status);
        return status;
    }

    return ZX_OK;
}
