// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_DSI_DW_DW_MIPI_DSI_REG_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_DSI_DW_DW_MIPI_DSI_REG_H_

#include <hwreg/bitfields.h>

/// define register access macros here

//////////////////////////////////////////////////
// DesignWare MIPI DSI Register Definitions
//////////////////////////////////////////////////
#define DW_DSI_VERSION (0x00 << 2)              // contains the vers of the DSI host controller
#define DW_DSI_PWR_UP (0x01 << 2)               // controls the power up of the core
#define DW_DSI_CLKMGR_CFG (0x02 << 2)           // configs the factor for internal dividers
#define DW_DSI_DPI_VCID (0x03 << 2)             // configs the Virt Chan ID for DPI traffic
#define DW_DSI_DPI_COLOR_CODING (0x04 << 2)     // configs DPI color coding
#define DW_DSI_DPI_CFG_POL (0x05 << 2)          // configs the polarity of DPI signals
#define DW_DSI_DPI_LP_CMD_TIM (0x06 << 2)       // configs the timing for lp cmds (in vid mode)
#define DW_DSI_DBI_VCID (0x07 << 2)             // configs Virtual Channel ID for DBI traffic
#define DW_DSI_DBI_CFG (0x08 << 2)              // configs the bit width of pixels for DBI
#define DW_DSI_DBI_PARTITIONING_EN (0x09 << 2)  // host partition DBI traffic automatically
#define DW_DSI_DBI_CMDSIZE (0x0A << 2)          // cmd size for auto partitioning of DBI
#define DW_DSI_PCKHDL_CFG (0x0B << 2)           // how EoTp, BTA, CRC and ECC are to be used
#define DW_DSI_GEN_VCID (0x0C << 2)             // Virt Channel ID of READ responses to store
#define DW_DSI_MODE_CFG (0x0D << 2)             // mode of op between Video or Command Mode
#define DW_DSI_VID_MODE_CFG (0x0E << 2)         // Video mode operation config
#define DW_DSI_VID_PKT_SIZE (0x0F << 2)         // video packet size
#define DW_DSI_VID_NUM_CHUNKS (0x10 << 2)       // number of chunks to use
#define DW_DSI_VID_NULL_SIZE (0x11 << 2)        // configs the size of null packets
#define DW_DSI_VID_HSA_TIME (0x12 << 2)         // configs the video HSA time
#define DW_DSI_VID_HBP_TIME (0x13 << 2)         // configs the video HBP time
#define DW_DSI_VID_HLINE_TIME (0x14 << 2)       // configs the overall time for each video line
#define DW_DSI_VID_VSA_LINES (0x15 << 2)        // configs the VSA period
#define DW_DSI_VID_VBP_LINES (0x16 << 2)        // configs the VBP period
#define DW_DSI_VID_VFP_LINES (0x17 << 2)        // configs the VFP period
#define DW_DSI_VID_VACTIVE_LINES (0x18 << 2)    // configs the vertical resolution of video
#define DW_DSI_EDPI_CMD_SIZE (0x19 << 2)        // configs the size of eDPI packets
#define DW_DSI_CMD_MODE_CFG (0x1A << 2)         // command mode operation config
#define DW_DSI_GEN_HDR (0x1B << 2)              // header for new packets
#define DW_DSI_GEN_PLD_DATA (0x1C << 2)         // payload for packets sent using the Gen i/f
#define DW_DSI_CMD_PKT_STATUS (0x1D << 2)       // info about FIFOs related to DBI and Gen i/f
#define DW_DSI_TO_CNT_CFG (0x1E << 2)           // counters that trig timeout errors
#define DW_DSI_HS_RD_TO_CNT (0x1F << 2)         // Peri Resp timeout after HS Rd operations
#define DW_DSI_LP_RD_TO_CNT (0x20 << 2)         // Peri Resp timeout after LP Rd operations
#define DW_DSI_HS_WR_TO_CNT (0x21 << 2)         // Peri Resp timeout after HS Wr operations
#define DW_DSI_LP_WR_TO_CNT (0x22 << 2)         // Peri Resp timeout after LP Wr operations
#define DW_DSI_BTA_TO_CNT (0x23 << 2)           // Peri Resp timeout after Bus Turnaround comp
#define DW_DSI_SDF_3D (0x24 << 2)               // 3D cntrl info for VSS packets in video mode.
#define DW_DSI_LPCLK_CTRL (0x25 << 2)           // non continuous clock in the clock lane.
#define DW_DSI_PHY_TMR_LPCLK_CFG (0x26 << 2)    // time for the clock lane
#define DW_DSI_PHY_TMR_CFG (0x27 << 2)          // time for the data lanes
#define DW_DSI_PHY_RSTZ (0x28 << 2)             // controls resets and the PLL of the D-PHY.
#define DW_DSI_PHY_IF_CFG (0x29 << 2)           // number of active lanes
#define DW_DSI_PHY_ULPS_CTRL (0x2A << 2)        // entering and leaving ULPS in the D- PHY.
#define DW_DSI_PHY_TX_TRIGGERS (0x2B << 2)      // pins that activate triggers in the D-PHY
#define DW_DSI_PHY_STATUS (0x2C << 2)           // contains info about the status of the D- PHY
#define DW_DSI_PHY_TST_CTRL0 (0x2D << 2)        // controls clock and clear pins of the D-PHY
#define DW_DSI_PHY_TST_CTRL1 (0x2E << 2)        // controls data and enable pins of the D-PHY
#define DW_DSI_INT_ST0 (0x3F << 2)              // status of intr from ack and D-PHY
#define DW_DSI_INT_ST1 (0x30 << 2)              // status of intr related to timeout, ECC, etc
#define DW_DSI_INT_MSK0 (0x31 << 2)             // masks interrupts that affect the INT_ST0 reg
#define DW_DSI_INT_MSK1 (0x32 << 2)             // masks interrupts that affect the INT_ST1 reg

class DsiDwVersionReg : public hwreg::RegisterBase<DsiDwVersionReg, uint32_t> {
 public:
  DEF_FIELD(31, 0, version);
  static auto Get() { return hwreg::RegisterAddr<DsiDwVersionReg>(DW_DSI_VERSION); }
};

class DsiDwPwrUpReg : public hwreg::RegisterBase<DsiDwPwrUpReg, uint32_t> {
 public:
  DEF_BIT(0, shutdown);
  static auto Get() { return hwreg::RegisterAddr<DsiDwPwrUpReg>(DW_DSI_PWR_UP); }
};

class DsiDwClkmgrCfgReg : public hwreg::RegisterBase<DsiDwClkmgrCfgReg, uint32_t> {
 public:
  DEF_FIELD(15, 8, to_clk_div);
  DEF_FIELD(7, 0, tx_esc_clk_div);
  static auto Get() { return hwreg::RegisterAddr<DsiDwClkmgrCfgReg>(DW_DSI_CLKMGR_CFG); }
};

class DsiDwDpiVcidReg : public hwreg::RegisterBase<DsiDwDpiVcidReg, uint32_t> {
 public:
  DEF_FIELD(1, 0, dpi_vcid);
  static auto Get() { return hwreg::RegisterAddr<DsiDwDpiVcidReg>(DW_DSI_DPI_VCID); }
};

class DsiDwDpiColorCodingReg : public hwreg::RegisterBase<DsiDwDpiColorCodingReg, uint32_t> {
 public:
  DEF_BIT(8, loosely18_en);
  DEF_FIELD(3, 0, dpi_color_coding);
  static auto Get() { return hwreg::RegisterAddr<DsiDwDpiColorCodingReg>(DW_DSI_DPI_COLOR_CODING); }
};

class DsiDwDpiCfgPolReg : public hwreg::RegisterBase<DsiDwDpiCfgPolReg, uint32_t> {
 public:
  DEF_BIT(4, colorm_active_low);
  DEF_BIT(3, shutd_active_low);
  DEF_BIT(2, hsync_active_low);
  DEF_BIT(1, vsync_active_low);
  DEF_BIT(0, dataen_active_low);
  static auto Get() { return hwreg::RegisterAddr<DsiDwDpiCfgPolReg>(DW_DSI_DPI_CFG_POL); }
};

class DsiDwDpiLpCmdTimReg : public hwreg::RegisterBase<DsiDwDpiLpCmdTimReg, uint32_t> {
 public:
  DEF_FIELD(23, 16, outvact_lpcmd_time);
  DEF_FIELD(7, 0, invact_lpcmd_time);
  static auto Get() { return hwreg::RegisterAddr<DsiDwDpiLpCmdTimReg>(DW_DSI_DPI_LP_CMD_TIM); }
};

class DsiDwDbiVcidReg : public hwreg::RegisterBase<DsiDwDbiVcidReg, uint32_t> {
 public:
  DEF_FIELD(1, 0, dbi_vcid);
  static auto Get() { return hwreg::RegisterAddr<DsiDwDbiVcidReg>(DW_DSI_DBI_VCID); }
};

class DsiDwDbiCfgReg : public hwreg::RegisterBase<DsiDwDbiCfgReg, uint32_t> {
 public:
  DEF_FIELD(17, 16, lut_size_conf);
  DEF_FIELD(11, 8, out_dbi_conf);
  DEF_FIELD(3, 0, in_dbi_conf);
  static auto Get() { return hwreg::RegisterAddr<DsiDwDbiCfgReg>(DW_DSI_DBI_CFG); }
};

class DsiDwDbiPartitioningEnReg : public hwreg::RegisterBase<DsiDwDbiPartitioningEnReg, uint32_t> {
 public:
  DEF_BIT(0, partitioning_en);
  static auto Get() {
    return hwreg::RegisterAddr<DsiDwDbiPartitioningEnReg>(DW_DSI_DBI_PARTITIONING_EN);
  }
};

class DsiDwDbiCmdsizeReg : public hwreg::RegisterBase<DsiDwDbiCmdsizeReg, uint32_t> {
 public:
  DEF_FIELD(31, 16, allowed_cmd_size);
  DEF_FIELD(15, 0, wr_cmd_size);
  static auto Get() { return hwreg::RegisterAddr<DsiDwDbiCmdsizeReg>(DW_DSI_DBI_CMDSIZE); }
};

class DsiDwPckhdlCfgReg : public hwreg::RegisterBase<DsiDwPckhdlCfgReg, uint32_t> {
 public:
  DEF_BIT(4, crc_rx_en);
  DEF_BIT(3, ecc_rx_en);
  DEF_BIT(2, bta_en);
  DEF_BIT(1, eotp_rx_en);
  DEF_BIT(0, eotp_tx_en);
  static auto Get() { return hwreg::RegisterAddr<DsiDwPckhdlCfgReg>(DW_DSI_PCKHDL_CFG); }
};

class DsiDwGenVcidReg : public hwreg::RegisterBase<DsiDwGenVcidReg, uint32_t> {
 public:
  DEF_FIELD(1, 0, gen_vcid_rx);
  static auto Get() { return hwreg::RegisterAddr<DsiDwGenVcidReg>(DW_DSI_GEN_VCID); }
};

class DsiDwModeCfgReg : public hwreg::RegisterBase<DsiDwModeCfgReg, uint32_t> {
 public:
  DEF_BIT(0, cmd_video_mode);
  static auto Get() { return hwreg::RegisterAddr<DsiDwModeCfgReg>(DW_DSI_MODE_CFG); }
};

class DsiDwVidModeCfgReg : public hwreg::RegisterBase<DsiDwVidModeCfgReg, uint32_t> {
 public:
  DEF_BIT(24, vpg_orientation);
  DEF_BIT(20, vpg_mode);
  DEF_BIT(16, vpg_en);
  DEF_BIT(15, lp_cmd_en);
  DEF_BIT(14, frame_bta_ack_en);
  DEF_BIT(13, lp_hfp_en);
  DEF_BIT(12, lp_hbp_en);
  DEF_BIT(11, lp_vact_en);
  DEF_BIT(10, lp_vfp_en);
  DEF_BIT(9, lp_vbp_en);
  DEF_BIT(8, lp_vsa_en);
  DEF_FIELD(1, 0, vid_mode_type);
  static auto Get() { return hwreg::RegisterAddr<DsiDwVidModeCfgReg>(DW_DSI_VID_MODE_CFG); }
};

class DsiDwVidPktSizeReg : public hwreg::RegisterBase<DsiDwVidPktSizeReg, uint32_t> {
 public:
  DEF_FIELD(13, 0, vid_pkt_size);
  static auto Get() { return hwreg::RegisterAddr<DsiDwVidPktSizeReg>(DW_DSI_VID_PKT_SIZE); }
};

class DsiDwVidNumChunksReg : public hwreg::RegisterBase<DsiDwVidNumChunksReg, uint32_t> {
 public:
  DEF_FIELD(12, 0, vid_num_chunks);
  static auto Get() { return hwreg::RegisterAddr<DsiDwVidNumChunksReg>(DW_DSI_VID_NUM_CHUNKS); }
};

class DsiDwVidNullSizeReg : public hwreg::RegisterBase<DsiDwVidNullSizeReg, uint32_t> {
 public:
  DEF_FIELD(12, 0, vid_null_size);
  static auto Get() { return hwreg::RegisterAddr<DsiDwVidNullSizeReg>(DW_DSI_VID_NULL_SIZE); }
};

class DsiDwVidHsaTimeReg : public hwreg::RegisterBase<DsiDwVidHsaTimeReg, uint32_t> {
 public:
  DEF_FIELD(11, 0, vid_hsa_time);
  static auto Get() { return hwreg::RegisterAddr<DsiDwVidHsaTimeReg>(DW_DSI_VID_HSA_TIME); }
};

class DsiDwVidHbpTimeReg : public hwreg::RegisterBase<DsiDwVidHbpTimeReg, uint32_t> {
 public:
  DEF_FIELD(11, 0, vid_hbp_time);
  static auto Get() { return hwreg::RegisterAddr<DsiDwVidHbpTimeReg>(DW_DSI_VID_HBP_TIME); }
};

class DsiDwVidHlineTimeReg : public hwreg::RegisterBase<DsiDwVidHlineTimeReg, uint32_t> {
 public:
  DEF_FIELD(14, 0, vid_hline_time);
  static auto Get() { return hwreg::RegisterAddr<DsiDwVidHlineTimeReg>(DW_DSI_VID_HLINE_TIME); }
};

class DsiDwVidVsaLinesReg : public hwreg::RegisterBase<DsiDwVidVsaLinesReg, uint32_t> {
 public:
  DEF_FIELD(9, 0, vsa_lines);
  static auto Get() { return hwreg::RegisterAddr<DsiDwVidVsaLinesReg>(DW_DSI_VID_VSA_LINES); }
};

class DsiDwVidVbpLinesReg : public hwreg::RegisterBase<DsiDwVidVbpLinesReg, uint32_t> {
 public:
  DEF_FIELD(9, 0, vbp_lines);
  static auto Get() { return hwreg::RegisterAddr<DsiDwVidVbpLinesReg>(DW_DSI_VID_VBP_LINES); }
};

class DsiDwVidVfpLinesReg : public hwreg::RegisterBase<DsiDwVidVfpLinesReg, uint32_t> {
 public:
  DEF_FIELD(9, 0, vfp_lines);
  static auto Get() { return hwreg::RegisterAddr<DsiDwVidVfpLinesReg>(DW_DSI_VID_VFP_LINES); }
};

class DsiDwVidVactiveLinesReg : public hwreg::RegisterBase<DsiDwVidVactiveLinesReg, uint32_t> {
 public:
  DEF_FIELD(13, 0, vactive_lines);
  static auto Get() {
    return hwreg::RegisterAddr<DsiDwVidVactiveLinesReg>(DW_DSI_VID_VACTIVE_LINES);
  }
};

class DsiDwEdpiCmdSizeReg : public hwreg::RegisterBase<DsiDwEdpiCmdSizeReg, uint32_t> {
 public:
  DEF_FIELD(15, 0, edpi_allowed_cmd_size);
  static auto Get() { return hwreg::RegisterAddr<DsiDwEdpiCmdSizeReg>(DW_DSI_EDPI_CMD_SIZE); }
};

class DsiDwCmdModeCfgReg : public hwreg::RegisterBase<DsiDwCmdModeCfgReg, uint32_t> {
 public:
  DEF_BIT(24, max_rd_pkt_size);
  DEF_BIT(19, dcs_lw_tx);
  DEF_BIT(18, dcs_sr_0p_tx);
  DEF_BIT(17, dcs_sw_1p_tx);
  DEF_BIT(16, dcs_sw_0p_tx);
  DEF_BIT(14, gen_lw_tx);
  DEF_BIT(13, gen_sr_2p_tx);
  DEF_BIT(12, gen_sr_1p_tx);
  DEF_BIT(11, gen_sr_0p_tx);
  DEF_BIT(10, gen_sw_2p_tx);
  DEF_BIT(9, gen_sw_1p_tx);
  DEF_BIT(8, gen_sw_0p_tx);
  DEF_BIT(1, ack_rqst_en);
  DEF_BIT(0, tear_fx_en);
  static auto Get() { return hwreg::RegisterAddr<DsiDwCmdModeCfgReg>(DW_DSI_CMD_MODE_CFG); }
};

class DsiDwGenHdrReg : public hwreg::RegisterBase<DsiDwGenHdrReg, uint32_t> {
 public:
  DEF_FIELD(23, 16, gen_wc_msbyte);
  DEF_FIELD(15, 8, gen_wc_lsbyte);
  DEF_FIELD(7, 6, gen_vc);
  DEF_FIELD(5, 0, gen_dt);
  static auto Get() { return hwreg::RegisterAddr<DsiDwGenHdrReg>(DW_DSI_GEN_HDR); }
};

class DsiDwGenPldDataReg : public hwreg::RegisterBase<DsiDwGenPldDataReg, uint32_t> {
 public:
  DEF_FIELD(31, 24, gen_pld_b4);
  DEF_FIELD(23, 16, gen_pld_b3);
  DEF_FIELD(15, 8, gen_pld_b2);
  DEF_FIELD(7, 0, gen_pld_b1);
  static auto Get() { return hwreg::RegisterAddr<DsiDwGenPldDataReg>(DW_DSI_GEN_PLD_DATA); }
};

class DsiDwCmdPktStatusReg : public hwreg::RegisterBase<DsiDwCmdPktStatusReg, uint32_t> {
 public:
  DEF_BIT(14, dbi_rd_cmd_busy);
  DEF_BIT(13, dbi_pld_r_full);
  DEF_BIT(12, dbi_pld_r_empty);
  DEF_BIT(11, dbi_pld_w_full);
  DEF_BIT(10, dbi_pld_w_empty);
  DEF_BIT(9, dbi_cmd_full);
  DEF_BIT(8, dbi_cmd_empy);
  DEF_BIT(6, gen_rd_cmd_busy);
  DEF_BIT(5, gen_pld_r_full);
  DEF_BIT(4, gen_pld_r_empty);
  DEF_BIT(3, gen_pld_w_full);
  DEF_BIT(2, gen_pld_w_empty);
  DEF_BIT(1, gen_cmd_full);
  DEF_BIT(0, gen_cmd_empty);
  static auto Get() { return hwreg::RegisterAddr<DsiDwCmdPktStatusReg>(DW_DSI_CMD_PKT_STATUS); }
};

class DsiDwToCntCfgReg : public hwreg::RegisterBase<DsiDwToCntCfgReg, uint32_t> {
 public:
  DEF_FIELD(31, 16, hstx_to_cnt);
  DEF_FIELD(15, 0, lprx_to_cnt);
  static auto Get() { return hwreg::RegisterAddr<DsiDwToCntCfgReg>(DW_DSI_TO_CNT_CFG); }
};

class DsiDwHsRdToCntReg : public hwreg::RegisterBase<DsiDwHsRdToCntReg, uint32_t> {
 public:
  DEF_FIELD(15, 0, hs_rd_to_cnt);
  static auto Get() { return hwreg::RegisterAddr<DsiDwHsRdToCntReg>(DW_DSI_HS_RD_TO_CNT); }
};

class DsiDwLpRdToCntReg : public hwreg::RegisterBase<DsiDwLpRdToCntReg, uint32_t> {
 public:
  DEF_FIELD(15, 0, lp_rd_to_cnt);
  static auto Get() { return hwreg::RegisterAddr<DsiDwLpRdToCntReg>(DW_DSI_LP_RD_TO_CNT); }
};

class DsiDwHsWrToCntReg : public hwreg::RegisterBase<DsiDwHsWrToCntReg, uint32_t> {
 public:
  DEF_BIT(24, presp_to_mode);
  DEF_FIELD(15, 0, hs_wr_to_cnt);
  static auto Get() { return hwreg::RegisterAddr<DsiDwHsWrToCntReg>(DW_DSI_HS_WR_TO_CNT); }
};

class DsiDwLpWrToCntReg : public hwreg::RegisterBase<DsiDwLpWrToCntReg, uint32_t> {
 public:
  DEF_FIELD(15, 0, lp_wr_to_cnt);
  static auto Get() { return hwreg::RegisterAddr<DsiDwLpWrToCntReg>(DW_DSI_LP_WR_TO_CNT); }
};

class DsiDwBtaToCntReg : public hwreg::RegisterBase<DsiDwBtaToCntReg, uint32_t> {
 public:
  DEF_FIELD(15, 0, bta_to_cnt);
  static auto Get() { return hwreg::RegisterAddr<DsiDwBtaToCntReg>(DW_DSI_BTA_TO_CNT); }
};

class DsiDwSdf3dReg : public hwreg::RegisterBase<DsiDwSdf3dReg, uint32_t> {
 public:
  DEF_BIT(16, send_3d_cfg);
  DEF_BIT(5, right_first);
  DEF_BIT(4, second_vsync);
  DEF_FIELD(3, 2, format_3d);
  DEF_FIELD(1, 0, mode_3d);
  static auto Get() { return hwreg::RegisterAddr<DsiDwSdf3dReg>(DW_DSI_SDF_3D); }
};

class DsiDwLpclkCtrlReg : public hwreg::RegisterBase<DsiDwLpclkCtrlReg, uint32_t> {
 public:
  DEF_BIT(1, auto_clklane_ctrl);
  DEF_BIT(0, phy_txrequestclkhs);
  static auto Get() { return hwreg::RegisterAddr<DsiDwLpclkCtrlReg>(DW_DSI_LPCLK_CTRL); }
};

class DsiDwPhyTmrLpclkCfgReg : public hwreg::RegisterBase<DsiDwPhyTmrLpclkCfgReg, uint32_t> {
 public:
  DEF_FIELD(25, 16, phy_clkhs2lp_time);
  DEF_FIELD(9, 0, phy_clklp2hs_time);
  static auto Get() {
    return hwreg::RegisterAddr<DsiDwPhyTmrLpclkCfgReg>(DW_DSI_PHY_TMR_LPCLK_CFG);
  }
};

class DsiDwPhyTmrCfgReg : public hwreg::RegisterBase<DsiDwPhyTmrCfgReg, uint32_t> {
 public:
  DEF_FIELD(25, 16, phy_hs2lp_time);
  DEF_FIELD(9, 0, phy_lp2hs_time);
  static auto Get() { return hwreg::RegisterAddr<DsiDwPhyTmrCfgReg>(DW_DSI_PHY_TMR_CFG); }
};

class DsiDwPhyRstzReg : public hwreg::RegisterBase<DsiDwPhyRstzReg, uint32_t> {
 public:
  DEF_BIT(3, phy_forcepll);
  DEF_BIT(2, phy_enableclk);
  DEF_BIT(1, phy_rstz);
  DEF_BIT(0, phy_shutdownz);
  static auto Get() { return hwreg::RegisterAddr<DsiDwPhyRstzReg>(DW_DSI_PHY_RSTZ); }
};

class DsiDwPhyIfCfgReg : public hwreg::RegisterBase<DsiDwPhyIfCfgReg, uint32_t> {
 public:
  DEF_FIELD(15, 8, phy_stop_wait_time);
  DEF_FIELD(1, 0, n_lanes);
  static auto Get() { return hwreg::RegisterAddr<DsiDwPhyIfCfgReg>(DW_DSI_PHY_IF_CFG); }
};

class DsiDwPhyUlpsCtrlReg : public hwreg::RegisterBase<DsiDwPhyUlpsCtrlReg, uint32_t> {
 public:
  DEF_BIT(3, phy_txexitulpslan);
  DEF_BIT(2, phy_txrequlpslan);
  DEF_BIT(1, phy_txexitulpsclk);
  DEF_BIT(0, phy_txrequlpsclk);
  static auto Get() { return hwreg::RegisterAddr<DsiDwPhyUlpsCtrlReg>(DW_DSI_PHY_ULPS_CTRL); }
};

class DsiDwPhyTxTriggersReg : public hwreg::RegisterBase<DsiDwPhyTxTriggersReg, uint32_t> {
 public:
  DEF_FIELD(3, 0, phy_tx_triggers);
  static auto Get() { return hwreg::RegisterAddr<DsiDwPhyTxTriggersReg>(DW_DSI_PHY_TX_TRIGGERS); }
};

class DsiDwPhyStatusReg : public hwreg::RegisterBase<DsiDwPhyStatusReg, uint32_t> {
 public:
  DEF_BIT(12, phy_ulpsactivenot3lane);
  DEF_BIT(11, phy_stopstate3lane);
  DEF_BIT(10, phy_ulpsactivenot2lane);
  DEF_BIT(9, phy_stopstate2lane);
  DEF_BIT(8, phy_ulpsactivenot1lane);
  DEF_BIT(7, phy_stopstate1lane);
  DEF_BIT(6, phy_rxulpsesc0lane);
  DEF_BIT(5, phy_ulpsactivenot0lane);
  DEF_BIT(4, phy_stopstate0lane);
  DEF_BIT(3, phy_ulpsactivenotclk);
  DEF_BIT(2, phy_stopstateclklane);
  DEF_BIT(1, phy_direction);
  DEF_BIT(0, phy_lock);
  static auto Get() { return hwreg::RegisterAddr<DsiDwPhyStatusReg>(DW_DSI_PHY_STATUS); }
};

class DsiDwPhyTstCtrl0Reg : public hwreg::RegisterBase<DsiDwPhyTstCtrl0Reg, uint32_t> {
 public:
  DEF_BIT(1, phy_testclk);
  DEF_BIT(0, phy_testclr);
  static auto Get() { return hwreg::RegisterAddr<DsiDwPhyTstCtrl0Reg>(DW_DSI_PHY_TST_CTRL0); }
};

class DsiDwPhyTstCtrl1Reg : public hwreg::RegisterBase<DsiDwPhyTstCtrl1Reg, uint32_t> {
 public:
  DEF_BIT(16, phy_testen);
  DEF_FIELD(15, 8, phy_testdout);
  DEF_FIELD(7, 0, phy_testin);
  static auto Get() { return hwreg::RegisterAddr<DsiDwPhyTstCtrl1Reg>(DW_DSI_PHY_TST_CTRL1); }
};

class DsiDwIntSt0Reg : public hwreg::RegisterBase<DsiDwIntSt0Reg, uint32_t> {
 public:
  DEF_BIT(20, dphy_errors_4);
  DEF_BIT(19, dphy_errors_3);
  DEF_BIT(18, dphy_errors_2);
  DEF_BIT(17, dphy_errors_1);
  DEF_BIT(16, dphy_errors_0);
  DEF_BIT(15, ack_with_err_15);
  DEF_BIT(14, ack_with_err_14);
  DEF_BIT(13, ack_with_err_13);
  DEF_BIT(12, ack_with_err_12);
  DEF_BIT(11, ack_with_err_11);
  DEF_BIT(10, ack_with_err_10);
  DEF_BIT(9, ack_with_err_9);
  DEF_BIT(8, ack_with_err_8);
  DEF_BIT(7, ack_with_err_7);
  DEF_BIT(6, ack_with_err_6);
  DEF_BIT(5, ack_with_err_5);
  DEF_BIT(4, ack_with_err_4);
  DEF_BIT(3, ack_with_err_3);
  DEF_BIT(2, ack_with_err_2);
  DEF_BIT(1, ack_with_err_1);
  DEF_BIT(0, ack_with_err_0);
  static auto Get() { return hwreg::RegisterAddr<DsiDwIntSt0Reg>(DW_DSI_INT_ST0); }
};

class DsiDwIntSt1Reg : public hwreg::RegisterBase<DsiDwIntSt1Reg, uint32_t> {
 public:
  DEF_BIT(17, dbi_ilegal_comm_err);
  DEF_BIT(16, dbi_pld_recv_err);
  DEF_BIT(15, dbi_pld_rd_err);
  DEF_BIT(14, dbi_pld_wr_err);
  DEF_BIT(13, dbi_cmd_wr_err);
  DEF_BIT(12, gen_pld_recev_err);
  DEF_BIT(11, gen_pld_rd_err);
  DEF_BIT(10, gen_pld_send_err);
  DEF_BIT(9, gen_pld_wr_err);
  DEF_BIT(8, gen_cmd_wr_err);
  DEF_BIT(7, dpi_pld_wr_err);
  DEF_BIT(6, eopt_err);
  DEF_BIT(5, pkt_size_err);
  DEF_BIT(4, crc_err);
  DEF_BIT(3, ecc_milti_err);
  DEF_BIT(2, ecc_single_err);
  DEF_BIT(1, to_lp_rx);
  DEF_BIT(0, to_hs_tx);
  static auto Get() { return hwreg::RegisterAddr<DsiDwIntSt1Reg>(DW_DSI_INT_ST1); }
};

class DsiDwIntMsk0Reg : public hwreg::RegisterBase<DsiDwIntMsk0Reg, uint32_t> {
 public:
  DEF_BIT(20, dphy_errors_4);
  DEF_BIT(19, dphy_errors_3);
  DEF_BIT(18, dphy_errors_2);
  DEF_BIT(17, dphy_errors_1);
  DEF_BIT(16, dphy_errors_0);
  DEF_BIT(15, ack_with_err_15);
  DEF_BIT(14, ack_with_err_14);
  DEF_BIT(13, ack_with_err_13);
  DEF_BIT(12, ack_with_err_12);
  DEF_BIT(11, ack_with_err_11);
  DEF_BIT(10, ack_with_err_10);
  DEF_BIT(9, ack_with_err_9);
  DEF_BIT(8, ack_with_err_8);
  DEF_BIT(7, ack_with_err_7);
  DEF_BIT(6, ack_with_err_6);
  DEF_BIT(5, ack_with_err_5);
  DEF_BIT(4, ack_with_err_4);
  DEF_BIT(3, ack_with_err_3);
  DEF_BIT(2, ack_with_err_2);
  DEF_BIT(1, ack_with_err_1);
  DEF_BIT(0, ack_with_err_0);
  static auto Get() { return hwreg::RegisterAddr<DsiDwIntMsk0Reg>(DW_DSI_INT_MSK0); }
};

class DsiDwIntMsk1Reg : public hwreg::RegisterBase<DsiDwIntMsk1Reg, uint32_t> {
 public:
  DEF_BIT(17, dbi_ilegal_comm_err);
  DEF_BIT(16, dbi_pld_recv_err);
  DEF_BIT(15, dbi_pld_rd_err);
  DEF_BIT(14, dbi_pld_wr_err);
  DEF_BIT(13, dbi_cmd_wr_err);
  DEF_BIT(12, gen_pld_recev_err);
  DEF_BIT(11, gen_pld_rd_err);
  DEF_BIT(10, gen_pld_send_err);
  DEF_BIT(9, gen_pld_wr_err);
  DEF_BIT(8, gen_cmd_wr_err);
  DEF_BIT(7, dpi_pld_wr_err);
  DEF_BIT(6, eopt_err);
  DEF_BIT(5, pkt_size_err);
  DEF_BIT(4, crc_err);
  DEF_BIT(3, ecc_milti_err);
  DEF_BIT(2, ecc_single_err);
  DEF_BIT(1, to_lp_rx);
  DEF_BIT(0, to_hs_tx);
  static auto Get() { return hwreg::RegisterAddr<DsiDwIntMsk1Reg>(DW_DSI_INT_MSK1); }
};

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_DSI_DW_DW_MIPI_DSI_REG_H_
