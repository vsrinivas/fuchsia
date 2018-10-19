// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-dsi-host.h"
#include <ddk/debug.h>

namespace astro_display {

#define READ32_MIPI_DSI_REG(a)              mipi_dsi_mmio_->Read32(a)
#define WRITE32_MIPI_DSI_REG(a, v)          mipi_dsi_mmio_->Write32(v, a)

#define READ32_HHI_REG(a)                   hhi_mmio_->Read32(a)
#define WRITE32_HHI_REG(a, v)               hhi_mmio_->Write32(v, a)

zx_status_t AmlDsiHost::HostModeInit(uint32_t opp, const DisplaySetting& disp_setting) {
    uint32_t lane_num = disp_setting.lane_num;

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
        WRITE32_REG(MIPI_DSI, DW_DSI_VID_PKT_SIZE, disp_setting.h_active);
        // Disable sending vid in chunk since they are ignored by DW host IP in burst mode
        WRITE32_REG(MIPI_DSI, DW_DSI_VID_NUM_CHUNKS, 0);
        WRITE32_REG(MIPI_DSI, DW_DSI_VID_NULL_SIZE, 0);

        // 4 Configure the video relative parameters according to the output type
        WRITE32_REG(MIPI_DSI, DW_DSI_VID_HLINE_TIME, disp_setting.h_period);
        WRITE32_REG(MIPI_DSI, DW_DSI_VID_HSA_TIME, disp_setting.hsync_width);
        WRITE32_REG(MIPI_DSI, DW_DSI_VID_HBP_TIME, disp_setting.hsync_bp);
        WRITE32_REG(MIPI_DSI, DW_DSI_VID_VSA_LINES, disp_setting.vsync_width);
        WRITE32_REG(MIPI_DSI, DW_DSI_VID_VBP_LINES, disp_setting.vsync_bp);
        WRITE32_REG(MIPI_DSI, DW_DSI_VID_VACTIVE_LINES, disp_setting.v_active);
        WRITE32_REG(MIPI_DSI, DW_DSI_VID_VFP_LINES, (disp_setting.v_period -
                    disp_setting.v_active - disp_setting.vsync_bp -
                    disp_setting.vsync_width));
    }

    // Internal dividers to divide lanebyteclk for timeout purposes
    WRITE32_REG(MIPI_DSI, DW_DSI_CLKMGR_CFG,
                (CLKMGR_CFG_TO_CLK_DIV(1)) |
                (CLKMGR_CFG_TX_ESC_CLK_DIV(phy_->GetLowPowerEscaseTime())));

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

void AmlDsiHost::PhyEnable() {
    WRITE32_REG(HHI, HHI_MIPI_CNTL0, MIPI_CNTL0_CMN_REF_GEN_CTRL(0x29) |
                MIPI_CNTL0_VREF_SEL(VREF_SEL_VR) |
                MIPI_CNTL0_LREF_SEL(LREF_SEL_L_ROUT) |
                MIPI_CNTL0_LBG_EN |
                MIPI_CNTL0_VR_TRIM_CNTL(0x7) |
                MIPI_CNTL0_VR_GEN_FROM_LGB_EN);
    WRITE32_REG(HHI, HHI_MIPI_CNTL1, MIPI_CNTL1_DSI_VBG_EN | MIPI_CNTL1_CTL);
    WRITE32_REG(HHI, HHI_MIPI_CNTL2, MIPI_CNTL2_DEFAULT_VAL); // 4 lane
}

void AmlDsiHost::PhyDisable() {
    WRITE32_REG(HHI, HHI_MIPI_CNTL0, 0);
    WRITE32_REG(HHI, HHI_MIPI_CNTL1, 0);
    WRITE32_REG(HHI, HHI_MIPI_CNTL2, 0);
}

void AmlDsiHost::HostOff(const DisplaySetting& disp_setting) {
    ZX_DEBUG_ASSERT(initialized_);
    // turn host off only if it's been fully turned on
    if (!host_on_) {
        return;
    }

    // Place dsi in command mode first
    HostModeInit(COMMAND_MODE, disp_setting);

    // Turn off LCD
    lcd_->Disable();

    // disable PHY
    PhyDisable();

    // finally shutdown host
    phy_->Shutdown();

    host_on_ = false;
}

zx_status_t AmlDsiHost::HostOn(const DisplaySetting& disp_setting) {
    ZX_DEBUG_ASSERT(initialized_);

    if (host_on_) {
        return ZX_OK;
    }

    // Enable MIPI PHY
    PhyEnable();

    // Create MIPI PHY object
    fbl::AllocChecker ac;
    phy_ = fbl::make_unique_checked<astro_display::AmlMipiPhy>(&ac);
    if (!ac.check()) {
        DISP_ERROR("Could not create AmlMipiPhy object\n");
        return ZX_ERR_NO_MEMORY;
    }
    zx_status_t status = phy_->Init(parent_, disp_setting.lane_num);
    if (status != ZX_OK) {
        DISP_ERROR("MIPI PHY Init failed!\n");
        return status;
    }

    // Load Phy configuration
    status = phy_->PhyCfgLoad(bitrate_);
    if (status != ZX_OK) {
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
    zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));

    // Enable LP transmission in CMD Mode
    WRITE32_REG(MIPI_DSI, DW_DSI_CMD_MODE_CFG,CMD_MODE_CFG_CMD_LP_ALL);

    // Packet header settings - Enable CRC and ECC. BTA will be enabled based on CMD
    WRITE32_REG(MIPI_DSI, DW_DSI_PCKHDL_CFG, PCKHDL_CFG_EN_CRC_ECC);

    // Initialize host in command mode first
    if ((status = HostModeInit(COMMAND_MODE, disp_setting)) != ZX_OK) {
        DISP_ERROR("Error during dsi host init! %d\n", status);
        return status;
    }

    // Initialize mipi dsi D-phy
    if ((status = phy_->Startup()) != ZX_OK) {
        DISP_ERROR("Error during MIPI D-PHY Initialization! %d\n", status);
        return status;
    }

    // Enable LP Clock
    SET_BIT32(MIPI_DSI, DW_DSI_LPCLK_CTRL, 1, LPCLK_CTRL_AUTOCLKLANE_CTRL, 1);

    // Load LCD Init values while in command mode
    lcd_ = fbl::make_unique_checked<astro_display::Lcd>(&ac, panel_type_);
    if (!ac.check()) {
        DISP_ERROR("Failed to create LCD object\n");
        return ZX_ERR_NO_MEMORY;
    }

    status = lcd_->Init(parent_);
    if (status != ZX_OK) {
        DISP_ERROR("Error during LCD Initialization! %d\n", status);
        return status;
    }

    status = lcd_->Enable();
    if (status != ZX_OK) {
        DISP_ERROR("Could not enable LCD! %d\n", status);
        return status;
    }

    // switch to video mode
    if ((status = HostModeInit(VIDEO_MODE, disp_setting)) != ZX_OK) {
        DISP_ERROR("Error during dsi host init! %d\n", status);
        return status;
    }

    // Host is On and Active at this point
    host_on_ = true;
    return ZX_OK;
}

zx_status_t AmlDsiHost::Init() {
    if (initialized_) {
        return ZX_OK;
    }

    zx_status_t status = device_get_protocol(parent_, ZX_PROTOCOL_PDEV, &pdev_);
    if (status != ZX_OK) {
        DISP_ERROR("AmlDsiHost: Could not get ZX_PROTOCOL_PDEV protocol\n");
        return status;
    }

    // Map MIPI DSI and HHI registers
    mmio_buffer_t mmio;
    status = pdev_map_mmio_buffer2(&pdev_, MMIO_MPI_DSI, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &mmio);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map MIPI DSI mmio\n");
        return status;
    }
    mipi_dsi_mmio_ = ddk::MmioBuffer(mmio);

    status = pdev_map_mmio_buffer2(&pdev_, MMIO_HHI, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &mmio);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map HHI mmio\n");
        return status;
    }
    hhi_mmio_ = ddk::MmioBuffer(mmio);

    initialized_ = true;
    return ZX_OK;
}

void AmlDsiHost::Dump() {
    ZX_DEBUG_ASSERT(initialized_);

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

} // namespace astro_display
