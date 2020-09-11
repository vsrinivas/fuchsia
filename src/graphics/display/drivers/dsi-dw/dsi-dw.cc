// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dsi-dw.h"

#include <fuchsia/sysmem/c/fidl.h>

#include <ddk/binding.h>
#include <ddk/metadata.h>
#include <ddk/metadata/display.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>

#include "dw-mipi-dsi-reg.h"

// Header Creation Macros
#define GEN_HDR_WC_MSB(x) ((x & 0xFF) << 16)
#define GEN_HDR_WC_LSB(x) ((x & 0xFF) << 8)
#define GEN_HDR_VC(x) ((x & 0x03) << 6)
#define GEN_HDR_DT(x) ((x & 0x3F) << 0)

namespace dsi_dw {

namespace {
constexpr uint32_t kPowerReset = 0;
constexpr uint32_t kPowerOn = 1;
constexpr uint32_t kPhyTestCtrlSet = 0x2;
constexpr uint32_t kPhyTestCtrlClr = 0x0;

constexpr uint32_t kDPhyTimeout = 200000;
constexpr uint32_t kPhyDelay = 6;
constexpr uint32_t kPhyStopWaitTime = 0x28;  // value from vendor

// Generic retry value used for BTA and FIFO related events
constexpr uint32_t kRetryMax = 20000;

constexpr uint32_t kMaxPldFifoDepth = 200;

constexpr uint32_t kBitPldRFull = 5;
constexpr uint32_t kBitPldREmpty = 4;
constexpr uint32_t kBitPldWFull = 3;
constexpr uint32_t kBitPldWEmpty = 2;
constexpr uint32_t kBitCmdFull = 1;
constexpr uint32_t kBitCmdEmpty = 0;

}  // namespace

zx_status_t DsiDw::DsiImplWriteReg(uint32_t reg, uint32_t val) {
  // TODO(payamm): Verify register offset is valid
  dsi_mmio_->Write32(val, reg);
  return ZX_OK;
}

zx_status_t DsiDw::DsiImplReadReg(uint32_t reg, uint32_t* val) {
  // TODO(payamm): Verify register offset is valid
  *val = dsi_mmio_->Read32(reg);
  return ZX_OK;
}

zx_status_t DsiDw::GetColorCode(color_code_t c, bool& packed, uint8_t& code) {
  zx_status_t status = ZX_OK;
  switch (c) {
    case COLOR_CODE_PACKED_16BIT_565:
      packed = true;
      code = 0;
      break;
    case COLOR_CODE_PACKED_18BIT_666:
      packed = true;
      code = 3;
      break;
    case COLOR_CODE_LOOSE_24BIT_666:
      packed = false;
      code = 3;
      break;
    case COLOR_CODE_PACKED_24BIT_888:
      packed = true;
      code = 5;
      break;
    default:
      status = ZX_ERR_INVALID_ARGS;
      break;
  }
  return status;
}

zx_status_t DsiDw::GetVideoMode(video_mode_t v, uint8_t& mode) {
  zx_status_t status = ZX_OK;
  switch (v) {
    case VIDEO_MODE_NON_BURST_PULSE:
      mode = 0;
      break;
    case VIDEO_MODE_NON_BURST_EVENT:
      mode = 1;
      break;
    case VIDEO_MODE_BURST:
      mode = 2;
      break;
    default:
      status = ZX_ERR_INVALID_ARGS;
  }
  return status;
}

void DsiDw::DsiImplPowerUp() {
  DsiDwPwrUpReg::Get().ReadFrom(&(*dsi_mmio_)).set_shutdown(kPowerOn).WriteTo(&(*dsi_mmio_));
}

void DsiDw::DsiImplPowerDown() {
  DsiDwPwrUpReg::Get().ReadFrom(&(*dsi_mmio_)).set_shutdown(kPowerReset).WriteTo(&(*dsi_mmio_));
}

bool DsiDw::DsiImplIsPoweredUp() {
  return (DsiDwPwrUpReg::Get().ReadFrom(&(*dsi_mmio_)).shutdown() == kPowerOn);
}

zx_status_t DsiDw::DsiImplEnableBist(uint32_t pattern) {
  // enable video mode
  DsiImplSetMode(DSI_MODE_VIDEO);

  DsiDwVidModeCfgReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_vpg_mode(1)
      .set_vpg_en(1)
      .WriteTo(&(*dsi_mmio_));
  return ZX_OK;
}
void DsiDw::DsiImplPhySendCode(uint32_t code, uint32_t parameter) {
  // Write code
  DsiDwPhyTstCtrl1Reg::Get().FromValue(0).set_reg_value(code).WriteTo(&(*dsi_mmio_));

  // Toggle PhyTestClk
  DsiDwPhyTstCtrl0Reg::Get().FromValue(0).set_reg_value(kPhyTestCtrlSet).WriteTo(&(*dsi_mmio_));
  DsiDwPhyTstCtrl0Reg::Get().FromValue(0).set_reg_value(kPhyTestCtrlClr).WriteTo(&(*dsi_mmio_));

  // Write parameter
  DsiDwPhyTstCtrl1Reg::Get().FromValue(0).set_reg_value(parameter).WriteTo(&(*dsi_mmio_));

  // Toggle PhyTestClk
  DsiDwPhyTstCtrl0Reg::Get().FromValue(0).set_reg_value(kPhyTestCtrlSet).WriteTo(&(*dsi_mmio_));
  DsiDwPhyTstCtrl0Reg::Get().FromValue(0).set_reg_value(kPhyTestCtrlClr).WriteTo(&(*dsi_mmio_));
}

void DsiDw::DsiImplPhyPowerUp() {
  DsiDwPhyRstzReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_phy_forcepll(1)
      .set_phy_enableclk(1)
      .set_phy_rstz(1)
      .set_phy_shutdownz(1)
      .WriteTo(&(*dsi_mmio_));
}

void DsiDw::DsiImplPhyPowerDown() {
  DsiDwPhyRstzReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_phy_rstz(0)
      .set_phy_shutdownz(0)
      .WriteTo(&(*dsi_mmio_));
}

zx_status_t DsiDw::DsiImplPhyWaitForReady() {
  int timeout = kDPhyTimeout;
  while ((DsiDwPhyStatusReg::Get().ReadFrom(&(*dsi_mmio_)).phy_lock() == 0) && timeout--) {
    zx_nanosleep(zx_deadline_after(ZX_USEC(kPhyDelay)));
  }
  if (timeout <= 0) {
    DSI_ERROR("Timeout! D-PHY did not lock\n");
    return ZX_ERR_TIMED_OUT;
  }

  timeout = kDPhyTimeout;
  while ((DsiDwPhyStatusReg::Get().ReadFrom(&(*dsi_mmio_)).phy_stopstateclklane() == 0) &&
         timeout--) {
    zx_nanosleep(zx_deadline_after(ZX_USEC(kPhyDelay)));
  }
  if (timeout <= 0) {
    DSI_ERROR("Timeout! D-PHY StopStateClk not set\n");
    return ZX_ERR_TIMED_OUT;
  }
  return ZX_OK;
}

zx_status_t DsiDw::DsiImplSendCmd(const mipi_dsi_cmd_t* cmd_list, size_t cmd_count) {
  zx_status_t status = ZX_OK;
  for (size_t i = 0; i < cmd_count && status == ZX_OK; i++) {
    status = SendCmd(cmd_list[i]);
  }
  return status;
}

void DsiDw::DsiImplSetMode(dsi_mode_t mode) {
  // Configure the operation mode (cmd or vid)
  DsiDwModeCfgReg::Get().ReadFrom(&(*dsi_mmio_)).set_cmd_video_mode(mode).WriteTo(&(*dsi_mmio_));
}

zx_status_t DsiDw::DsiImplConfig(const dsi_config_t* dsi_config) {
  const display_setting_t disp_setting = dsi_config->display_setting;
  const designware_config_t dw_cfg =
      *(static_cast<designware_config_t*>(dsi_config->vendor_config_buffer));

  bool packed;
  uint8_t code;
  uint8_t video_mode;
  zx_status_t status = GetColorCode(dsi_config->color_coding, packed, code);
  if (status != ZX_OK) {
    DSI_ERROR("Invalid/Unsupported Color Coding\n");
    return status;
  }

  status = GetVideoMode(dsi_config->video_mode_type, video_mode);
  if (status != ZX_OK) {
    DSI_ERROR("Invalid/Unsupported video mode\n");
    return status;
  }

  // Enable LP transmission in CMD Mode
  DsiDwCmdModeCfgReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_max_rd_pkt_size(1)
      .set_dcs_lw_tx(1)
      .set_dcs_sr_0p_tx(1)
      .set_dcs_sw_1p_tx(1)
      .set_dcs_sw_0p_tx(1)
      .set_gen_lw_tx(1)
      .set_gen_sr_2p_tx(1)
      .set_gen_sr_1p_tx(1)
      .set_gen_sr_0p_tx(1)
      .set_gen_sw_2p_tx(1)
      .set_gen_sw_1p_tx(1)
      .set_gen_sw_0p_tx(1)
      .WriteTo(&(*dsi_mmio_));

  // Packet header settings - Enable CRC and ECC. BTA will be enabled based on CMD
  DsiDwPckhdlCfgReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_crc_rx_en(1)
      .set_ecc_rx_en(1)
      .WriteTo(&(*dsi_mmio_));

  // DesignWare DSI Host Setup based on MIPI DSI Host Controller User Guide (Sec 3.1.1)

  // 1. Global configuration: Lane number and PHY stop wait time
  DsiDwPhyIfCfgReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_phy_stop_wait_time(kPhyStopWaitTime)
      .set_n_lanes(disp_setting.lane_num - 1)
      .WriteTo(&(*dsi_mmio_));

  // 2.1 Configure virtual channel
  DsiDwDpiVcidReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_dpi_vcid(MIPI_DSI_VIRTUAL_CHAN_ID)
      .WriteTo(&(*dsi_mmio_));

  // 2.2, Configure Color format
  DsiDwDpiColorCodingReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_loosely18_en(!packed)
      .set_dpi_color_coding(code)
      .WriteTo(&(*dsi_mmio_));
  // 2.3 Configure Signal polarity - Keep as default
  DsiDwDpiCfgPolReg::Get().FromValue(0).set_reg_value(0).WriteTo(&(*dsi_mmio_));

  // The following values are relevent for video mode
  // 3.1 Configure low power transitions and video mode type
  DsiDwVidModeCfgReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_vpg_en(0)
      .set_lp_cmd_en(1)
      .set_frame_bta_ack_en(1)
      .set_lp_hfp_en(1)
      .set_lp_hbp_en(1)
      .set_lp_vact_en(1)
      .set_lp_vfp_en(1)
      .set_lp_vbp_en(1)
      .set_lp_vsa_en(1)
      .set_vid_mode_type(video_mode)
      .WriteTo(&(*dsi_mmio_));

  // Define the max pkt size during Low Power mode
  DsiDwDpiLpCmdTimReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_outvact_lpcmd_time(dw_cfg.lp_cmd_pkt_size)
      .set_invact_lpcmd_time(dw_cfg.lp_cmd_pkt_size)
      .WriteTo(&(*dsi_mmio_));

  // 3.2   Configure video packet size settings
  DsiDwVidPktSizeReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_vid_pkt_size(disp_setting.h_active)
      .WriteTo(&(*dsi_mmio_));

  // Disable sending vid in chunk since they are ignored by DW host IP in burst mode
  DsiDwVidNumChunksReg::Get().FromValue(0).set_reg_value(0).WriteTo(&(*dsi_mmio_));
  DsiDwVidNullSizeReg::Get().FromValue(0).set_reg_value(0).WriteTo(&(*dsi_mmio_));

  // 4 Configure the video relative parameters according to the output type

  DsiDwVidHsaTimeReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_vid_hsa_time(disp_setting.hsync_width)
      .WriteTo(&(*dsi_mmio_));

  DsiDwVidHbpTimeReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_vid_hbp_time(disp_setting.hsync_bp)
      .WriteTo(&(*dsi_mmio_));

  DsiDwVidHlineTimeReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_vid_hline_time(disp_setting.h_period)
      .WriteTo(&(*dsi_mmio_));

  DsiDwVidVsaLinesReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_vsa_lines(disp_setting.vsync_width)
      .WriteTo(&(*dsi_mmio_));

  DsiDwVidVbpLinesReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_vbp_lines(disp_setting.vsync_bp)
      .WriteTo(&(*dsi_mmio_));

  DsiDwVidVactiveLinesReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_vactive_lines(disp_setting.v_active)
      .WriteTo(&(*dsi_mmio_));

  DsiDwVidVfpLinesReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_vfp_lines(disp_setting.v_period - disp_setting.v_active - disp_setting.vsync_bp -
                     disp_setting.vsync_width)
      .WriteTo(&(*dsi_mmio_));

  // Internal dividers to divide lanebyteclk for timeout purposes
  DsiDwClkmgrCfgReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_to_clk_div(1)
      .set_tx_esc_clk_div(dw_cfg.lp_escape_time)
      .WriteTo(&(*dsi_mmio_));

  // Setup Phy Timers as provided by vendor
  DsiDwPhyTmrLpclkCfgReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_phy_clkhs2lp_time(dw_cfg.phy_timer_clkhs_to_lp)
      .set_phy_clklp2hs_time(dw_cfg.phy_timer_clklp_to_hs)
      .WriteTo(&(*dsi_mmio_));
  DsiDwPhyTmrCfgReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_phy_hs2lp_time(dw_cfg.phy_timer_hs_to_lp)
      .set_phy_lp2hs_time(dw_cfg.phy_timer_lp_to_hs)
      .WriteTo(&(*dsi_mmio_));

  DsiDwLpclkCtrlReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_auto_clklane_ctrl(dw_cfg.auto_clklane)
      .set_phy_txrequestclkhs(1)
      .WriteTo(&(*dsi_mmio_));

  return ZX_OK;
}

void DsiDw::DsiImplPrintDsiRegisters() {
  DSI_INFO("%s: DUMPING DSI HOST REGS\n", __func__);
  DSI_INFO("DW_DSI_VERSION = 0x%x\n", DsiDwVersionReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_PWR_UP = 0x%x\n", DsiDwPwrUpReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_CLKMGR_CFG = 0x%x\n",
           DsiDwClkmgrCfgReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_DPI_VCID = 0x%x\n", DsiDwDpiVcidReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_DPI_COLOR_CODING = 0x%x\n",
           DsiDwDpiColorCodingReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_DPI_CFG_POL = 0x%x\n",
           DsiDwDpiCfgPolReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_DPI_LP_CMD_TIM = 0x%x\n",
           DsiDwDpiLpCmdTimReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_DBI_VCID = 0x%x\n", DsiDwDbiVcidReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_DBI_CFG = 0x%x\n", DsiDwDbiCfgReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_DBI_PARTITIONING_EN = 0x%x\n",
           DsiDwDbiPartitioningEnReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_DBI_CMDSIZE = 0x%x\n",
           DsiDwDbiCmdsizeReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_PCKHDL_CFG = 0x%x\n",
           DsiDwPckhdlCfgReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_GEN_VCID = 0x%x\n", DsiDwGenVcidReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_MODE_CFG = 0x%x\n", DsiDwModeCfgReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_VID_MODE_CFG = 0x%x\n",
           DsiDwVidModeCfgReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_VID_PKT_SIZE = 0x%x\n",
           DsiDwVidPktSizeReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_VID_NUM_CHUNKS = 0x%x\n",
           DsiDwVidNumChunksReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_VID_NULL_SIZE = 0x%x\n",
           DsiDwVidNullSizeReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_VID_HSA_TIME = 0x%x\n",
           DsiDwVidHsaTimeReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_VID_HBP_TIME = 0x%x\n",
           DsiDwVidHbpTimeReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_VID_HLINE_TIME = 0x%x\n",
           DsiDwVidHlineTimeReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_VID_VSA_LINES = 0x%x\n",
           DsiDwVidVsaLinesReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_VID_VBP_LINES = 0x%x\n",
           DsiDwVidVbpLinesReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_VID_VFP_LINES = 0x%x\n",
           DsiDwVidVfpLinesReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_VID_VACTIVE_LINES = 0x%x\n",
           DsiDwVidVactiveLinesReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_EDPI_CMD_SIZE = 0x%x\n",
           DsiDwEdpiCmdSizeReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_CMD_MODE_CFG = 0x%x\n",
           DsiDwCmdModeCfgReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_GEN_HDR = 0x%x\n", DsiDwGenHdrReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_GEN_PLD_DATA = 0x%x\n",
           DsiDwGenPldDataReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_CMD_PKT_STATUS = 0x%x\n",
           DsiDwCmdPktStatusReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_TO_CNT_CFG = 0x%x\n",
           DsiDwToCntCfgReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_HS_RD_TO_CNT = 0x%x\n",
           DsiDwHsRdToCntReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_LP_RD_TO_CNT = 0x%x\n",
           DsiDwLpRdToCntReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_HS_WR_TO_CNT = 0x%x\n",
           DsiDwHsWrToCntReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_LP_WR_TO_CNT = 0x%x\n",
           DsiDwLpWrToCntReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_BTA_TO_CNT = 0x%x\n",
           DsiDwBtaToCntReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_SDF_3D = 0x%x\n", DsiDwSdf3dReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_LPCLK_CTRL = 0x%x\n",
           DsiDwLpclkCtrlReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_PHY_TMR_LPCLK_CFG = 0x%x\n",
           DsiDwPhyTmrLpclkCfgReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_PHY_TMR_CFG = 0x%x\n",
           DsiDwPhyTmrCfgReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_PHY_RSTZ = 0x%x\n", DsiDwPhyRstzReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_PHY_IF_CFG = 0x%x\n",
           DsiDwPhyIfCfgReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_PHY_ULPS_CTRL = 0x%x\n",
           DsiDwPhyUlpsCtrlReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_PHY_TX_TRIGGERS = 0x%x\n",
           DsiDwPhyTxTriggersReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_PHY_STATUS = 0x%x\n",
           DsiDwPhyStatusReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_PHY_TST_CTRL0 = 0x%x\n",
           DsiDwPhyTstCtrl0Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_PHY_TST_CTRL1 = 0x%x\n",
           DsiDwPhyTstCtrl1Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_INT_ST0 = 0x%x\n", DsiDwIntSt0Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_INT_ST1 = 0x%x\n", DsiDwIntSt1Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_INT_MSK0 = 0x%x\n", DsiDwIntMsk0Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  DSI_INFO("DW_DSI_INT_MSK1 = 0x%x\n", DsiDwIntMsk1Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
}

inline bool DsiDw::IsPldREmpty() {
  return (DsiDwCmdPktStatusReg::Get().ReadFrom(&(*dsi_mmio_)).gen_pld_r_empty() == 1);
}

inline bool DsiDw::IsPldRFull() {
  return (DsiDwCmdPktStatusReg::Get().ReadFrom(&(*dsi_mmio_)).gen_pld_r_full() == 1);
}

inline bool DsiDw::IsPldWEmpty() {
  return (DsiDwCmdPktStatusReg::Get().ReadFrom(&(*dsi_mmio_)).gen_pld_w_empty() == 1);
}

inline bool DsiDw::IsPldWFull() {
  return (DsiDwCmdPktStatusReg::Get().ReadFrom(&(*dsi_mmio_)).gen_pld_w_full() == 1);
}

inline bool DsiDw::IsCmdEmpty() {
  return (DsiDwCmdPktStatusReg::Get().ReadFrom(&(*dsi_mmio_)).gen_cmd_empty() == 1);
}

inline bool DsiDw::IsCmdFull() {
  return (DsiDwCmdPktStatusReg::Get().ReadFrom(&(*dsi_mmio_)).gen_cmd_full() == 1);
}

zx_status_t DsiDw::WaitforFifo(uint32_t bit, bool val) {
  int retry = kRetryMax;
  while (((DsiDwCmdPktStatusReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value() >> bit) & 1) != val &&
         retry--) {
    zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
  }
  if (retry <= 0) {
    return ZX_ERR_TIMED_OUT;
  }
  return ZX_OK;
}

zx_status_t DsiDw::WaitforPldWNotFull() { return WaitforFifo(kBitPldWFull, 0); }

zx_status_t DsiDw::WaitforPldWEmpty() { return WaitforFifo(kBitPldWEmpty, 1); }

zx_status_t DsiDw::WaitforPldRFull() { return WaitforFifo(kBitPldRFull, 1); }

zx_status_t DsiDw::WaitforPldRNotEmpty() { return WaitforFifo(kBitPldREmpty, 0); }

zx_status_t DsiDw::WaitforCmdNotFull() { return WaitforFifo(kBitCmdFull, 0); }

zx_status_t DsiDw::WaitforCmdEmpty() { return WaitforFifo(kBitCmdEmpty, 1); }

void DsiDw::DumpCmd(const mipi_dsi_cmd_t& cmd) {
  zxlogf(ERROR, "\n\t\t MIPI DSI Command:");
  zxlogf(ERROR, "\t\t\t\t VIC = 0x%x (%d)", cmd.virt_chn_id, cmd.virt_chn_id);
  zxlogf(ERROR, "\t\t\t\t Data Type = 0x%x (%d)", cmd.dsi_data_type, cmd.dsi_data_type);
  zxlogf(ERROR, "\t\t\t\t ACK = 0x%x (%d)", cmd.flags, cmd.flags);
  zxlogf(ERROR, "\t\t\t\t Payload size = 0x%lx (%ld)", cmd.pld_data_count, cmd.pld_data_count);
  zxlogf(ERROR, "\t\t\t\t Payload Data: [");

  for (size_t i = 0; i < cmd.pld_data_count; i++) {
    zxlogf(ERROR, "0x%x, ", cmd.pld_data_list[i]);
  }
  zxlogf(ERROR, "]\n");
}

zx_status_t DsiDw::GenericPayloadRead(uint32_t* data) {
  // make sure there is something valid to read from payload fifo
  if (WaitforPldRNotEmpty() != ZX_OK) {
    DSI_ERROR("Timeout! PLD R FIFO remained empty\n");
    return ZX_ERR_TIMED_OUT;
  }
  *data = DsiDwGenPldDataReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value();
  return ZX_OK;
}

zx_status_t DsiDw::GenericHdrWrite(uint32_t data) {
  // make sure cmd fifo is not full before writing into it
  if (WaitforCmdNotFull() != ZX_OK) {
    DSI_ERROR("Timeout! CMD FIFO remained full\n");
    return ZX_ERR_TIMED_OUT;
  }
  DsiDwGenHdrReg::Get().FromValue(0).set_reg_value(data).WriteTo(&(*dsi_mmio_));
  return ZX_OK;
}

zx_status_t DsiDw::GenericPayloadWrite(uint32_t data) {
  // Make sure PLD_W is not full before writing into it
  if (WaitforPldWNotFull() != ZX_OK) {
    DSI_ERROR("Timeout! PLD W FIFO remained full!\n");
    return ZX_ERR_TIMED_OUT;
  }
  DsiDwGenPldDataReg::Get().FromValue(0).set_reg_value(data).WriteTo(&(*dsi_mmio_));
  return ZX_OK;
}

void DsiDw::EnableBta() {
  // enable ack req after each packet transmission
  DsiDwCmdModeCfgReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_ack_rqst_en(MIPI_DSI_ACK)
      .WriteTo(&(*dsi_mmio_));
  // enable But Turn-Around request
  DsiDwPckhdlCfgReg::Get().ReadFrom(&(*dsi_mmio_)).set_bta_en(MIPI_DSI_ACK).WriteTo(&(*dsi_mmio_));
}

void DsiDw::DisableBta() {
  // disable ack req after each packet transmission
  DsiDwCmdModeCfgReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_ack_rqst_en(MIPI_DSI_NO_ACK)
      .WriteTo(&(*dsi_mmio_));

  // disable But Turn-Around request
  DsiDwPckhdlCfgReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_bta_en(MIPI_DSI_NO_ACK)
      .WriteTo(&(*dsi_mmio_));
}

zx_status_t DsiDw::WaitforBtaAck() {
  int retry = kRetryMax;
  while (DsiDwCmdPktStatusReg::Get().ReadFrom(&(*dsi_mmio_)).gen_rd_cmd_busy() && retry--) {
    zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
  }
  if (retry <= 0) {
    DSI_ERROR("Timeout waiting for read to complete!\n");
    return ZX_ERR_TIMED_OUT;
  }
  return ZX_OK;
}

// MIPI DSI Functions as implemented by DWC IP
zx_status_t DsiDw::GenWriteShort(const mipi_dsi_cmd_t& cmd) {
  // Sanity check payload data and size
  if ((cmd.pld_data_count > 2) || (cmd.pld_data_count > 0 && cmd.pld_data_list == nullptr) ||
      (cmd.dsi_data_type & MIPI_DSI_DT_GEN_SHORT_WRITE_0) != MIPI_DSI_DT_GEN_SHORT_WRITE_0) {
    DSI_ERROR("Invalid Gen short cmd sent\n");
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t regVal = 0;
  regVal |= GEN_HDR_DT(cmd.dsi_data_type);
  regVal |= GEN_HDR_VC(cmd.virt_chn_id);
  if (cmd.pld_data_count >= 1) {
    regVal |= GEN_HDR_WC_LSB(cmd.pld_data_list[0]);
  }
  if (cmd.pld_data_count == 2) {
    regVal |= GEN_HDR_WC_MSB(cmd.pld_data_list[1]);
  }

  return GenericHdrWrite(regVal);
}

zx_status_t DsiDw::DcsWriteShort(const mipi_dsi_cmd_t& cmd) {
  // Sanity check payload data and size
  if ((cmd.pld_data_count > 1) || (cmd.pld_data_list == nullptr) ||
      (cmd.dsi_data_type & MIPI_DSI_DT_DCS_SHORT_WRITE_0) != MIPI_DSI_DT_DCS_SHORT_WRITE_0) {
    DSI_ERROR("Invalid DCS short command\n");
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t regVal = 0;
  regVal |= GEN_HDR_DT(cmd.dsi_data_type);
  regVal |= GEN_HDR_VC(cmd.virt_chn_id);
  regVal |= GEN_HDR_WC_LSB(cmd.pld_data_list[0]);
  if (cmd.pld_data_count == 1) {
    regVal |= GEN_HDR_WC_MSB(cmd.pld_data_list[1]);
  }

  return GenericHdrWrite(regVal);
}

// This function writes a generic long command. We can only write a maximum of FIFO_DEPTH
// to the payload fifo. This value is implementation specific.
zx_status_t DsiDw::GenWriteLong(const mipi_dsi_cmd_t& cmd) {
  zx_status_t status = ZX_OK;
  uint32_t pld_data_idx = 0;  // payload data index
  uint32_t regVal = 0;
  ZX_DEBUG_ASSERT(cmd.pld_data_count < kMaxPldFifoDepth);
  size_t ts = cmd.pld_data_count;  // initial transfer size

  if (ts > 0 && cmd.pld_data_list == nullptr) {
    DSI_ERROR("Invalid generic long write command\n");
    return ZX_ERR_INVALID_ARGS;
  }

  // first write complete words
  while (ts >= 4) {
    regVal = cmd.pld_data_list[pld_data_idx + 0] << 0 | cmd.pld_data_list[pld_data_idx + 1] << 8 |
             cmd.pld_data_list[pld_data_idx + 2] << 16 | cmd.pld_data_list[pld_data_idx + 3] << 24;
    pld_data_idx += 4;
    if ((status = GenericPayloadWrite(regVal)) != ZX_OK) {
      DSI_ERROR("Generic Payload write failed! %d\n", status);
      return status;
    }
    ts -= 4;
  }

  // Write remaining bytes
  if (ts > 0) {
    regVal = cmd.pld_data_list[pld_data_idx++] << 0;
    if (ts > 1) {
      regVal |= cmd.pld_data_list[pld_data_idx++] << 8;
    }
    if (ts > 2) {
      regVal |= cmd.pld_data_list[pld_data_idx++] << 16;
    }
    if ((status = GenericPayloadWrite(regVal)) != ZX_OK) {
      DSI_ERROR("Generic Payload write failed! %d\n", status);
      return status;
    }
  }

  // At this point, we have written all of our mipi payload to FIFO. Let's transmit it
  regVal = 0;
  regVal |= GEN_HDR_DT(cmd.dsi_data_type);
  regVal |= GEN_HDR_VC(cmd.virt_chn_id);
  regVal |= GEN_HDR_WC_LSB(static_cast<uint32_t>(cmd.pld_data_count) & 0xFF);
  regVal |= GEN_HDR_WC_MSB((cmd.pld_data_count & 0xFF00) >> 16);

  return GenericHdrWrite(regVal);
}

zx_status_t DsiDw::GenRead(const mipi_dsi_cmd_t& cmd) {
  uint32_t regVal = 0;
  zx_status_t status = ZX_OK;

  // valid cmd packet
  if ((cmd.rsp_data_list == nullptr) || (cmd.pld_data_count > 2) ||
      (cmd.pld_data_count > 0 && cmd.pld_data_list == nullptr)) {
    DSI_ERROR("Invalid generic read command\n");
    return ZX_ERR_INVALID_ARGS;
  }

  // Check whether max return packet size should be set
  if (cmd.flags & MIPI_DSI_CMD_FLAGS_SET_MAX) {
    // We will set the max return size as rlen
    regVal |= GEN_HDR_VC(cmd.virt_chn_id);
    regVal |= MIPI_DSI_DT_SET_MAX_RET_PKT;
    regVal |= GEN_HDR_WC_LSB(static_cast<uint32_t>(cmd.rsp_data_count) & 0xFF);
    regVal |= GEN_HDR_WC_MSB((static_cast<uint32_t>(cmd.rsp_data_count) >> 8) & 0xFF);

    if ((status = GenericHdrWrite(regVal)) != ZX_OK) {
      // no need to print extra info
      return status;
    }
  }

  regVal = 0;
  regVal |= GEN_HDR_DT(cmd.dsi_data_type);
  regVal |= GEN_HDR_VC(cmd.virt_chn_id);
  if (cmd.pld_data_count >= 1) {
    regVal |= GEN_HDR_WC_LSB(cmd.pld_data_list[0]);
  }
  if (cmd.pld_data_count == 2) {
    regVal |= GEN_HDR_WC_MSB(cmd.pld_data_list[1]);
  }

  // Packet is ready. Let's enable BTA first
  EnableBta();

  if ((status = GenericHdrWrite(regVal)) != ZX_OK) {
    // no need to print extra error msg
    return status;
  }

  if ((status = WaitforBtaAck()) != ZX_OK) {
    // bta never returned. no need to print extra error msg
    return status;
  }

  // Got ACK. Let's start reading
  // We should only read rlen worth of DATA. Let's hope the device is not sending
  // more than it should.
  size_t ts = cmd.rsp_data_count;
  uint32_t rsp_data_idx = 0;
  uint32_t data;
  while (ts >= 4) {
    if ((status = GenericPayloadRead(&data)) != ZX_OK) {
      DSI_ERROR("Something went wrong when reading data. Aborting\n");
      return status;
    }
    cmd.rsp_data_list[rsp_data_idx++] = static_cast<uint8_t>((data >> 0) & 0xFF);
    cmd.rsp_data_list[rsp_data_idx++] = static_cast<uint8_t>((data >> 8) & 0xFF);
    cmd.rsp_data_list[rsp_data_idx++] = static_cast<uint8_t>((data >> 16) & 0xFF);
    cmd.rsp_data_list[rsp_data_idx++] = static_cast<uint8_t>((data >> 24) & 0xFF);
    ts -= 4;
  }

  // Read out remaining bytes
  if (ts > 0) {
    if ((status = GenericPayloadRead(&data)) != ZX_OK) {
      DSI_ERROR("Something went wrong when reading data. Aborting\n");
      return status;
    }
    cmd.rsp_data_list[rsp_data_idx++] = (data >> 0) & 0xFF;
    if (ts > 1) {
      cmd.rsp_data_list[rsp_data_idx++] = (data >> 8) & 0xFF;
    }
    if (ts > 2) {
      cmd.rsp_data_list[rsp_data_idx++] = (data >> 16) & 0xFF;
    }
  }

  // we are done. Display BTA
  DisableBta();
  return status;
}

zx_status_t DsiDw::SendCmd(const mipi_dsi_cmd_t& cmd) {
  zx_status_t status = ZX_OK;

  switch (cmd.dsi_data_type) {
    case MIPI_DSI_DT_GEN_SHORT_WRITE_0:
    case MIPI_DSI_DT_GEN_SHORT_WRITE_1:
    case MIPI_DSI_DT_GEN_SHORT_WRITE_2:
      status = GenWriteShort(cmd);
      break;
    case MIPI_DSI_DT_GEN_LONG_WRITE:
    case MIPI_DSI_DT_DCS_LONG_WRITE:
      status = GenWriteLong(cmd);
      break;
    case MIPI_DSI_DT_GEN_SHORT_READ_0:
    case MIPI_DSI_DT_GEN_SHORT_READ_1:
    case MIPI_DSI_DT_GEN_SHORT_READ_2:
    case MIPI_DSI_DT_DCS_READ_0:
      status = GenRead(cmd);
      break;
    case MIPI_DSI_DT_DCS_SHORT_WRITE_0:
    case MIPI_DSI_DT_DCS_SHORT_WRITE_1:
      status = DcsWriteShort(cmd);
      break;
    default:
      DSI_ERROR("Unsupported/Invalid DSI Command type %d\n", cmd.dsi_data_type);
      status = ZX_ERR_INVALID_ARGS;
  }

  if (status != ZX_OK) {
    DSI_ERROR("Something went wrong is sending command\n");
    DumpCmd(cmd);
  }

  return status;
}

void DsiDw::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void DsiDw::DdkRelease() { delete this; }

zx_status_t DsiDw::Bind() {
  zx_status_t status = device_get_protocol(parent_, ZX_PROTOCOL_PDEV, &pdev_proto_);
  if (status != ZX_OK) {
    DSI_ERROR("Could not get parent protocol (%d)\n", status);
    return status;
  }

  // Map DSI registers
  mmio_buffer_t mmio;
  status = pdev_map_mmio_buffer(&pdev_proto_, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DSI_ERROR("Could not map DSI mmio (%d)\n", status);
    return status;
  }

  dsi_mmio_ = ddk::MmioBuffer(mmio);

  status = DdkAdd("dw-dsi");
  if (status != ZX_OK) {
    DSI_ERROR("could not add device %d\n", status);
  }
  return status;
}

// main bind function called from dev manager
zx_status_t dsi_dw_bind(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<dsi_dw::DsiDw>(&ac, parent);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto status = dev->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

static constexpr zx_driver_ops_t dsi_dw_ops = [] {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = dsi_dw_bind;
  return ops;
}();

}  // namespace dsi_dw

// clang-format off
ZIRCON_DRIVER_BEGIN(dsi_dw, dsi_dw::dsi_dw_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_DW_DSI),
ZIRCON_DRIVER_END(dsi_dw)
