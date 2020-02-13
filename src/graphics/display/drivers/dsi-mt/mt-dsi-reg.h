// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_DSI_MT_MT_DSI_REG_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_DSI_MT_MT_DSI_REG_H_

#include <hwreg/bitfields.h>
#include <hwreg/mmio.h>

namespace dsi_mt {

//////////////////////////////////////////////////
// DSI Registers
//////////////////////////////////////////////////
#define DSI_START (0x0000)
#define DSI_STA (0x0004)
#define DSI_INTEN (0x0008)
#define DSI_INTSTA (0x000C)
#define DSI_COM_CTRL (0x0010)
#define DSI_MODE_CTRL (0x0014)
#define DSI_TXRX_CTRL (0x0018)
#define DSI_PSCTRL (0x001C)
#define DSI_VSA_NL (0x0020)
#define DSI_VBP_NL (0x0024)
#define DSI_VFP_NL (0x0028)
#define DSI_VACT_NL (0x002C)
#define DSI_HSA_WC (0x0050)
#define DSI_HBP_WC (0x0054)
#define DSI_HFP_WC (0x0058)
#define DSI_BLLP_WC (0x005C)
#define DSI_CMDQ_SIZE (0x0060)
#define DSI_HSTX_CKL_WC (0x0064)
#define DSI_RX_DATA0 (0x0074)
#define DSI_RX_DATA1 (0x0078)
#define DSI_RX_DATA2 (0x007c)
#define DSI_RX_DATA3 (0x0080)
#define DSI_RACK (0x0084)
#define DSI_TRIG_STA (0x0088)
#define DSI_MEM_CONTI (0x0090)
#define DSI_FRM_BC (0x0094)
#define DSI_PHY_LCPAT (0x0100)
#define DSI_PHY_LCCON (0x0104)
#define DSI_PHY_LD0CON (0x0108)
#define DSI_PHY_TIMECON0 (0x0110)
#define DSI_PHY_TIMECON1 (0x0114)
#define DSI_PHY_TIMECON2 (0x0118)
#define DSI_PHY_TIMECON3 (0x011C)
#define DSI_PHY_TIMECON4 (0x0120)
#define DSI_VM_CMD_CON (0x0130)
#define DSI_VM_CMD_DATA0 (0x0134)
#define DSI_VM_CMD_DATA4 (0x0138)
#define DSI_VM_CMD_DATA8 (0x013C)
#define DSI_VM_CMD_DATAC (0x0140)
#define DSI_CKSM_OUT (0x0144)
#define DSI_STATE_DBG0 (0x0148)
#define DSI_STATE_DBG1 (0x014C)
#define DSI_STATE_DBG2 (0x0150)
#define DSI_STATE_DBG3 (0x0154)
#define DSI_STATE_DBG4 (0x0158)
#define DSI_STATE_DBG5 (0x015C)
#define DSI_STATE_DBG6 (0x0160)
#define DSI_STATE_DBG7 (0x0164)
#define DSI_STATE_DBG8 (0x0168)
#define DSI_STATE_DBG9 (0x016C)
#define DSI_DEBUG_SEL (0x0170)
#define DSI_BIST_PATTERN (0x0178)
#define DSI_BIST_CON (0x017C)
#define DSI_CMDQ0 (0x0180)
#define DSI_CMDQ(x) (0x0180 + (x * 4))

class DsiStartReg : public hwreg::RegisterBase<DsiStartReg, uint32_t> {
 public:
  DEF_BIT(16, vm_cmd_start);
  DEF_BIT(2, sleepout_start);
  DEF_BIT(0, dsi_start);
  static auto Get() { return hwreg::RegisterAddr<DsiStartReg>(DSI_START); }
};

class DsiStaReg : public hwreg::RegisterBase<DsiStaReg, uint32_t> {
 public:
  DEF_BIT(7, contention_err);
  DEF_BIT(6, false_ctrl_err);
  DEF_BIT(5, lpdt_sync_err);
  DEF_BIT(4, esc_entry_err);
  DEF_BIT(1, buffer_underrun);
  static auto Get() { return hwreg::RegisterAddr<DsiStaReg>(DSI_STA); }
};

class DsiIntEnReg : public hwreg::RegisterBase<DsiIntEnReg, uint32_t> {
 public:
  DEF_BIT(6, sleepout_done_int);
  DEF_BIT(5, vm_cmd_done);
  DEF_BIT(4, ext_te_rdy);
  DEF_BIT(3, vm_done);
  DEF_BIT(2, lprx_te_rdy);
  DEF_BIT(1, cmd_done);
  DEF_BIT(0, lprx_rd_rdy);
  static auto Get() { return hwreg::RegisterAddr<DsiIntEnReg>(DSI_INTEN); }
};

class DsiIntStaReg : public hwreg::RegisterBase<DsiIntStaReg, uint32_t> {
 public:
  DEF_BIT(31, dsi_busy);
  DEF_BIT(6, sleepout_done_int);
  DEF_BIT(5, vm_cmd_done);
  DEF_BIT(4, ext_te_rdy);
  DEF_BIT(3, vm_done);
  DEF_BIT(2, lprx_te_rdy);
  DEF_BIT(1, cmd_done);
  DEF_BIT(0, lprx_rd_rdy);
  static auto Get() { return hwreg::RegisterAddr<DsiIntStaReg>(DSI_INTSTA); }
};

class DsiComCtrlReg : public hwreg::RegisterBase<DsiComCtrlReg, uint32_t> {
 public:
  DEF_BIT(4, dsi_dual_en);
  DEF_BIT(1, dsi_en);
  DEF_BIT(0, dsi_reset);
  static auto Get() { return hwreg::RegisterAddr<DsiComCtrlReg>(DSI_COM_CTRL); }
};

class DsiModeCtrlReg : public hwreg::RegisterBase<DsiModeCtrlReg, uint32_t> {
 public:
  DEF_BIT(21, skip_vm_stop);
  DEF_BIT(20, sleep_mode);
  DEF_BIT(19, c2v_switch_on);
  DEF_BIT(18, v2c_switch_on);
  DEF_BIT(17, mix_mode);
  DEF_BIT(16, frame_mode);
  DEF_FIELD(1, 0, mode_con);
  static auto Get() { return hwreg::RegisterAddr<DsiModeCtrlReg>(DSI_MODE_CTRL); }
};

class DsiTxRxCtrlReg : public hwreg::RegisterBase<DsiTxRxCtrlReg, uint32_t> {
 public:
  DEF_BIT(16, hstx_cklp_en);
  DEF_FIELD(15, 12, max_rtn_size);
  DEF_BIT(11, te_auto_sync);
  DEF_BIT(10, ext_te_edge_sel);
  DEF_BIT(9, ext_te_en);
  DEF_BIT(8, te_freerun);
  DEF_BIT(7, hstx_bllp_en);
  DEF_BIT(6, hstx_dis_eot);
  DEF_FIELD(5, 2, lane_num);
  DEF_FIELD(1, 0, vc_num);
  static auto Get() { return hwreg::RegisterAddr<DsiTxRxCtrlReg>(DSI_TXRX_CTRL); }
};

class DsiPsCtrlReg : public hwreg::RegisterBase<DsiPsCtrlReg, uint32_t> {
 public:
  DEF_FIELD(17, 16, ps_sel);
  DEF_FIELD(13, 0, ps_wc);
  static auto Get() { return hwreg::RegisterAddr<DsiPsCtrlReg>(DSI_PSCTRL); }
};

class DsiVsaNlReg : public hwreg::RegisterBase<DsiVsaNlReg, uint32_t> {
 public:
  DEF_FIELD(6, 0, vsa);
  static auto Get() { return hwreg::RegisterAddr<DsiVsaNlReg>(DSI_VSA_NL); }
};

class DsiVbpNlReg : public hwreg::RegisterBase<DsiVbpNlReg, uint32_t> {
 public:
  DEF_FIELD(6, 0, vbp);
  static auto Get() { return hwreg::RegisterAddr<DsiVbpNlReg>(DSI_VBP_NL); }
};

class DsiVfpNlReg : public hwreg::RegisterBase<DsiVfpNlReg, uint32_t> {
 public:
  DEF_FIELD(6, 0, vfp);
  static auto Get() { return hwreg::RegisterAddr<DsiVfpNlReg>(DSI_VFP_NL); }
};

class DsiVactNlReg : public hwreg::RegisterBase<DsiVactNlReg, uint32_t> {
 public:
  DEF_FIELD(11, 0, vact);
  static auto Get() { return hwreg::RegisterAddr<DsiVactNlReg>(DSI_VACT_NL); }
};

class DsiHsaWcReg : public hwreg::RegisterBase<DsiHsaWcReg, uint32_t> {
 public:
  DEF_FIELD(11, 0, hsa);
  static auto Get() { return hwreg::RegisterAddr<DsiHsaWcReg>(DSI_HSA_WC); }
};

class DsiHbpWcReg : public hwreg::RegisterBase<DsiHbpWcReg, uint32_t> {
 public:
  DEF_FIELD(11, 0, hbp);
  static auto Get() { return hwreg::RegisterAddr<DsiHbpWcReg>(DSI_HBP_WC); }
};

class DsiHfpWcReg : public hwreg::RegisterBase<DsiHfpWcReg, uint32_t> {
 public:
  DEF_FIELD(11, 0, hfp);
  static auto Get() { return hwreg::RegisterAddr<DsiHfpWcReg>(DSI_HFP_WC); }
};

class DsiBllpWcReg : public hwreg::RegisterBase<DsiBllpWcReg, uint32_t> {
 public:
  DEF_FIELD(11, 0, bllp);
  static auto Get() { return hwreg::RegisterAddr<DsiBllpWcReg>(DSI_BLLP_WC); }
};

class DsiCmdqSizeReg : public hwreg::RegisterBase<DsiCmdqSizeReg, uint32_t> {
 public:
  DEF_FIELD(5, 0, cmdq_reg_size);
  static auto Get() { return hwreg::RegisterAddr<DsiCmdqSizeReg>(DSI_CMDQ_SIZE); }
};

class DsiHstxCklWcReg : public hwreg::RegisterBase<DsiHstxCklWcReg, uint32_t> {
 public:
  DEF_FIELD(15, 2, cklp_wc);
  static auto Get() { return hwreg::RegisterAddr<DsiHstxCklWcReg>(DSI_HSTX_CKL_WC); }
};

class DsiRxData03Reg : public hwreg::RegisterBase<DsiRxData03Reg, uint32_t> {
 public:
  DEF_FIELD(31, 24, byte3);
  DEF_FIELD(23, 16, byte2);
  DEF_FIELD(15, 8, byte1);
  DEF_FIELD(7, 0, byte0);
  static auto Get() { return hwreg::RegisterAddr<DsiRxData03Reg>(DSI_RX_DATA0); }
};

class DsiRxData47Reg : public hwreg::RegisterBase<DsiRxData47Reg, uint32_t> {
 public:
  DEF_FIELD(31, 24, byte7);
  DEF_FIELD(23, 16, byte6);
  DEF_FIELD(15, 8, byte5);
  DEF_FIELD(7, 0, byte4);
  static auto Get() { return hwreg::RegisterAddr<DsiRxData47Reg>(DSI_RX_DATA1); }
};

class DsiRxData8bReg : public hwreg::RegisterBase<DsiRxData8bReg, uint32_t> {
 public:
  DEF_FIELD(31, 24, byteb);
  DEF_FIELD(23, 16, bytea);
  DEF_FIELD(15, 8, byte9);
  DEF_FIELD(7, 0, byte8);
  static auto Get() { return hwreg::RegisterAddr<DsiRxData8bReg>(DSI_RX_DATA2); }
};

class DsiRxDataCReg : public hwreg::RegisterBase<DsiRxDataCReg, uint32_t> {
 public:
  DEF_FIELD(31, 24, bytef);
  DEF_FIELD(23, 16, bytee);
  DEF_FIELD(15, 8, byted);
  DEF_FIELD(7, 0, bytec);
  static auto Get() { return hwreg::RegisterAddr<DsiRxDataCReg>(DSI_RX_DATA3); }
};

class DsiRackReg : public hwreg::RegisterBase<DsiRackReg, uint32_t> {
 public:
  DEF_BIT(1, rack_bypass);
  DEF_BIT(0, rack);
  static auto Get() { return hwreg::RegisterAddr<DsiRackReg>(DSI_RACK); }
};

class DsiTrigStaReg : public hwreg::RegisterBase<DsiTrigStaReg, uint32_t> {
 public:
  DEF_BIT(5, direction);
  DEF_BIT(4, rx_ulps);
  DEF_BIT(3, rx_trig_3);
  DEF_BIT(2, rx_trig_2);
  DEF_BIT(1, rx_trig_1);
  DEF_BIT(0, rx_trig_0);
  static auto Get() { return hwreg::RegisterAddr<DsiTrigStaReg>(DSI_TRIG_STA); }
};

class DsiMemContReg : public hwreg::RegisterBase<DsiMemContReg, uint32_t> {
 public:
  DEF_FIELD(15, 0, rwmem_cont);
  static auto Get() { return hwreg::RegisterAddr<DsiMemContReg>(DSI_MEM_CONTI); }
};

class DsiFrmBcReg : public hwreg::RegisterBase<DsiFrmBcReg, uint32_t> {
 public:
  DEF_FIELD(20, 0, frm_bc);
  static auto Get() { return hwreg::RegisterAddr<DsiFrmBcReg>(DSI_FRM_BC); }
};

class DsiPhyLcpatReg : public hwreg::RegisterBase<DsiPhyLcpatReg, uint32_t> {
 public:
  DEF_FIELD(7, 0, lc_hstx_ck_pat);
  static auto Get() { return hwreg::RegisterAddr<DsiPhyLcpatReg>(DSI_PHY_LCPAT); }
};

class DsiPhyLcconReg : public hwreg::RegisterBase<DsiPhyLcconReg, uint32_t> {
 public:
  DEF_BIT(2, lc_wakeup_en);
  DEF_BIT(1, lc_ulpm_en);
  DEF_BIT(0, lc_hstx_en);
  static auto Get() { return hwreg::RegisterAddr<DsiPhyLcconReg>(DSI_PHY_LCCON); }
};

class DsiPhyLd0ConReg : public hwreg::RegisterBase<DsiPhyLd0ConReg, uint32_t> {
 public:
  DEF_BIT(3, lx_ulpm_as_l0);
  DEF_BIT(2, l0_wakeup_en);
  DEF_BIT(1, l0_ulpm_en);
  DEF_BIT(0, l0_rm_trig_en);
  static auto Get() { return hwreg::RegisterAddr<DsiPhyLd0ConReg>(DSI_PHY_LD0CON); }
};

class DsiPhyTimeCon0Reg : public hwreg::RegisterBase<DsiPhyTimeCon0Reg, uint32_t> {
 public:
  DEF_FIELD(31, 24, hs_trail);
  DEF_FIELD(23, 16, hs_zero);
  DEF_FIELD(15, 8, hs_prep);
  DEF_FIELD(7, 0, lpx);
  static auto Get() { return hwreg::RegisterAddr<DsiPhyTimeCon0Reg>(DSI_PHY_TIMECON0); }
};

class DsiPhyTimeCon1Reg : public hwreg::RegisterBase<DsiPhyTimeCon1Reg, uint32_t> {
 public:
  DEF_FIELD(31, 24, hs_exit);
  DEF_FIELD(23, 16, ta_get);
  DEF_FIELD(15, 8, ta_sure);
  DEF_FIELD(7, 0, ta_go);
  static auto Get() { return hwreg::RegisterAddr<DsiPhyTimeCon1Reg>(DSI_PHY_TIMECON1); }
};

class DsiPhyTimeCon2Reg : public hwreg::RegisterBase<DsiPhyTimeCon2Reg, uint32_t> {
 public:
  DEF_FIELD(31, 24, clk_trail);
  DEF_FIELD(23, 16, clk_zero);
  DEF_FIELD(7, 0, cont_det);
  static auto Get() { return hwreg::RegisterAddr<DsiPhyTimeCon2Reg>(DSI_PHY_TIMECON2); }
};

class DsiPhyTimeCon3Reg : public hwreg::RegisterBase<DsiPhyTimeCon3Reg, uint32_t> {
 public:
  DEF_FIELD(23, 16, clk_exit);
  DEF_FIELD(15, 8, clk_post);
  DEF_FIELD(7, 0, clk_prep);
  static auto Get() { return hwreg::RegisterAddr<DsiPhyTimeCon3Reg>(DSI_PHY_TIMECON3); }
};

class DsiPhyTimeCon4Reg : public hwreg::RegisterBase<DsiPhyTimeCon4Reg, uint32_t> {
 public:
  DEF_FIELD(19, 0, ulps_wakeup);
  static auto Get() { return hwreg::RegisterAddr<DsiPhyTimeCon4Reg>(DSI_PHY_TIMECON4); }
};

class DsiVmCmdConReg : public hwreg::RegisterBase<DsiVmCmdConReg, uint32_t> {
 public:
  DEF_FIELD(31, 24, cm_data_1);
  DEF_FIELD(23, 16, cm_data_0);
  DEF_FIELD(15, 8, cm_data_id);
  DEF_BIT(5, ts_vfp_en);
  DEF_BIT(4, ts_vbp_en);
  DEF_BIT(3, ts_vsa_en);
  DEF_BIT(2, time_sel);
  DEF_BIT(1, long_pkt);
  DEF_BIT(0, vm_cmd_en);
  static auto Get() { return hwreg::RegisterAddr<DsiVmCmdConReg>(DSI_VM_CMD_CON); }
};

class DsiVmCmdData0Reg : public hwreg::RegisterBase<DsiVmCmdData0Reg, uint32_t> {
 public:
  DEF_FIELD(31, 0, word);
  static auto Get() { return hwreg::RegisterAddr<DsiVmCmdData0Reg>(DSI_VM_CMD_DATA0); }
};

class DsiVmCmdData4Reg : public hwreg::RegisterBase<DsiVmCmdData4Reg, uint32_t> {
 public:
  DEF_FIELD(31, 0, word);
  static auto Get() { return hwreg::RegisterAddr<DsiVmCmdData4Reg>(DSI_VM_CMD_DATA4); }
};

class DsiVmCmdData8Reg : public hwreg::RegisterBase<DsiVmCmdData8Reg, uint32_t> {
 public:
  DEF_FIELD(31, 0, word);
  static auto Get() { return hwreg::RegisterAddr<DsiVmCmdData8Reg>(DSI_VM_CMD_DATA8); }
};

class DsiVmCmdDataCReg : public hwreg::RegisterBase<DsiVmCmdDataCReg, uint32_t> {
 public:
  DEF_FIELD(31, 0, word);
  static auto Get() { return hwreg::RegisterAddr<DsiVmCmdDataCReg>(DSI_VM_CMD_DATAC); }
};

class DsiCksmOutReg : public hwreg::RegisterBase<DsiCksmOutReg, uint32_t> {
 public:
  DEF_FIELD(15, 0, checksum);
  static auto Get() { return hwreg::RegisterAddr<DsiCksmOutReg>(DSI_CKSM_OUT); }
};

class DsiStateDbg0Reg : public hwreg::RegisterBase<DsiStateDbg0Reg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<DsiStateDbg0Reg>(DSI_STATE_DBG0); }
};

class DsiStateDbg1Reg : public hwreg::RegisterBase<DsiStateDbg1Reg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<DsiStateDbg1Reg>(DSI_STATE_DBG1); }
};

class DsiStateDbg2Reg : public hwreg::RegisterBase<DsiStateDbg2Reg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<DsiStateDbg2Reg>(DSI_STATE_DBG2); }
};

class DsiStateDbg3Reg : public hwreg::RegisterBase<DsiStateDbg3Reg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<DsiStateDbg3Reg>(DSI_STATE_DBG3); }
};

class DsiStateDbg4Reg : public hwreg::RegisterBase<DsiStateDbg4Reg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<DsiStateDbg4Reg>(DSI_STATE_DBG4); }
};

class DsiStateDbg5Reg : public hwreg::RegisterBase<DsiStateDbg5Reg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<DsiStateDbg5Reg>(DSI_STATE_DBG5); }
};

class DsiStateDbg6Reg : public hwreg::RegisterBase<DsiStateDbg6Reg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<DsiStateDbg6Reg>(DSI_STATE_DBG6); }
};

class DsiStateDbg7Reg : public hwreg::RegisterBase<DsiStateDbg7Reg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<DsiStateDbg7Reg>(DSI_STATE_DBG7); }
};

class DsiStateDbg8Reg : public hwreg::RegisterBase<DsiStateDbg8Reg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<DsiStateDbg8Reg>(DSI_STATE_DBG8); }
};

class DsiStateDbg9Reg : public hwreg::RegisterBase<DsiStateDbg9Reg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<DsiStateDbg9Reg>(DSI_STATE_DBG9); }
};

class DsiDebugSelReg : public hwreg::RegisterBase<DsiDebugSelReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<DsiDebugSelReg>(DSI_DEBUG_SEL); }
};

class DsiBistPatternReg : public hwreg::RegisterBase<DsiBistPatternReg, uint32_t> {
 public:
  DEF_FIELD(31, 0, bist_pattern);
  static auto Get() { return hwreg::RegisterAddr<DsiBistPatternReg>(DSI_BIST_PATTERN); }
};

class DsiBistConReg : public hwreg::RegisterBase<DsiBistConReg, uint32_t> {
 public:
  DEF_FIELD(23, 16, bist_timing);
  DEF_BIT(15, vsync_inv);
  DEF_FIELD(11, 8, bist_lane_num);
  DEF_BIT(7, sel_pat_mode);
  DEF_BIT(6, pll_ck_mon);
  DEF_BIT(5, bist_lane1_mux);
  DEF_BIT(4, bist_hs_free);
  DEF_BIT(3, bist_specified_pattern);
  DEF_BIT(2, bist_fix_pattern);
  DEF_BIT(1, bist_enable);
  DEF_BIT(0, bist_mode);
  static auto Get() { return hwreg::RegisterAddr<DsiBistConReg>(DSI_BIST_CON); }
};

class CmdQReg : public hwreg::RegisterBase<CmdQReg, uint32_t> {
 public:
  DEF_FIELD(31, 24, data_1);
  DEF_FIELD(23, 16, data_0);
  DEF_FIELD(15, 8, data_id);
  DEF_BIT(5, te);
  DEF_BIT(4, cl);
  DEF_BIT(3, hs);
  DEF_BIT(2, bta);
  DEF_FIELD(1, 0, type);
  static auto Get(uint32_t x) { return hwreg::RegisterAddr<CmdQReg>(DSI_CMDQ0 + (x * 4)); }
};

}  // namespace dsi_mt

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_DSI_MT_MT_DSI_REG_H_
