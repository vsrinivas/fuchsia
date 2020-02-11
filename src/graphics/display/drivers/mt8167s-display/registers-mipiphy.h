// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_MT8167S_DISPLAY_REGISTERS_MIPIPHY_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_MT8167S_DISPLAY_REGISTERS_MIPIPHY_H_

#include <hwreg/bitfields.h>
#include <hwreg/mmio.h>

//////////////////////////////////////////////////
// MIPI TX Registers
//////////////////////////////////////////////////
#define MIPI_TX_CON (0x0000)
#define MIPI_TX_CLOCK_LANE (0x0004)
#define MIPI_TX_DATA_LANE0 (0x0008)
#define MIPI_TX_DATA_LANE1 (0x000C)
#define MIPI_TX_DATA_LANE2 (0x0010)
#define MIPI_TX_DATA_LANE3 (0x0014)
#define MIPI_TX_TOP_CON (0x0040)
#define MIPI_TX_BG_CON (0x0044)
#define MIPI_TX_PLL_CON0 (0x0050)
#define MIPI_TX_PLL_CON1 (0x0054)
#define MIPI_TX_PLL_CON2 (0x0058)
#define MIPI_TX_PLL_CON3 (0x005C)
#define MIPI_TX_PLL_CHG (0x0060)
#define MIPI_TX_PLL_TOP (0x0064)
#define MIPI_TX_PLL_PWR (0x0068)
#define MIPI_TX_RGS (0x0070)
#define MIPI_TX_SW_CTRL (0x0080)
#define MIPI_TX_SW_CTRL_CON0 (0x0084)
#define MIPI_TX_SW_CTRL_CON1 (0x0084)

namespace mt8167s_display {

class MipiTxConReg : public hwreg::RegisterBase<MipiTxConReg, uint32_t> {
 public:
  DEF_FIELD(14, 12, lprxcd_sel);
  DEF_BIT(11, lptx_clmp_en);
  DEF_BIT(10, dsiclk_freq_sel);
  DEF_FIELD(9, 8, phyclk_sel);
  DEF_FIELD(6, 4, ld_idx_sel);
  DEF_FIELD(3, 2, bclk_sel);
  DEF_BIT(1, ckg_ldoout_en);
  DEF_BIT(0, ldocore_en);
  static auto Get() { return hwreg::RegisterAddr<MipiTxConReg>(MIPI_TX_CON); }
};

class MipiTxClockLaneReg : public hwreg::RegisterBase<MipiTxClockLaneReg, uint32_t> {
 public:
  DEF_BIT(12, cklane_en);
  DEF_FIELD(11, 8, rt_code);
  DEF_BIT(5, phi_sel);
  DEF_BIT(4, lptx_iminus);
  DEF_BIT(3, lptx_iplus2);
  DEF_BIT(2, lptx_iplus1);
  DEF_BIT(1, loopback_en);
  DEF_BIT(0, ldoout_en);
  static auto Get() { return hwreg::RegisterAddr<MipiTxClockLaneReg>(MIPI_TX_CLOCK_LANE); }
};

class MipiTxDataLane0Reg : public hwreg::RegisterBase<MipiTxDataLane0Reg, uint32_t> {
 public:
  DEF_BIT(11, cklane_en);
  DEF_FIELD(10, 7, rt_code);
  DEF_BIT(6, lpcd_iminus);
  DEF_BIT(5, lpcd_iplus);
  DEF_BIT(4, lptx_iminus);
  DEF_BIT(3, lptx_iplus2);
  DEF_BIT(2, lptx_iplus1);
  DEF_BIT(1, loopback_en);
  DEF_BIT(0, ldoout_en);
  static auto Get() { return hwreg::RegisterAddr<MipiTxDataLane0Reg>(MIPI_TX_DATA_LANE0); }
};

class MipiTxDataLane1Reg : public hwreg::RegisterBase<MipiTxDataLane1Reg, uint32_t> {
 public:
  DEF_BIT(9, cklane_en);
  DEF_FIELD(8, 5, rt_code);
  DEF_BIT(4, lptx_iminus);
  DEF_BIT(3, lptx_iplus2);
  DEF_BIT(2, lptx_iplus1);
  DEF_BIT(1, loopback_en);
  DEF_BIT(0, ldoout_en);
  static auto Get() { return hwreg::RegisterAddr<MipiTxDataLane1Reg>(MIPI_TX_DATA_LANE1); }
};

class MipiTxDataLane2Reg : public hwreg::RegisterBase<MipiTxDataLane2Reg, uint32_t> {
 public:
  DEF_BIT(9, cklane_en);
  DEF_FIELD(8, 5, rt_code);
  DEF_BIT(4, lptx_iminus);
  DEF_BIT(3, lptx_iplus2);
  DEF_BIT(2, lptx_iplus1);
  DEF_BIT(1, loopback_en);
  DEF_BIT(0, ldoout_en);
  static auto Get() { return hwreg::RegisterAddr<MipiTxDataLane2Reg>(MIPI_TX_DATA_LANE2); }
};

class MipiTxDataLane3Reg : public hwreg::RegisterBase<MipiTxDataLane3Reg, uint32_t> {
 public:
  DEF_BIT(9, cklane_en);
  DEF_FIELD(8, 5, rt_code);
  DEF_BIT(4, lptx_iminus);
  DEF_BIT(3, lptx_iplus2);
  DEF_BIT(2, lptx_iplus1);
  DEF_BIT(1, loopback_en);
  DEF_BIT(0, ldoout_en);
  static auto Get() { return hwreg::RegisterAddr<MipiTxDataLane3Reg>(MIPI_TX_DATA_LANE3); }
};

class MipiTxTopConReg : public hwreg::RegisterBase<MipiTxTopConReg, uint32_t> {
 public:
  DEF_BIT(11, pad_tie_low_en);
  DEF_FIELD(10, 8, aio_sel);
  DEF_FIELD(7, 4, imp_cal_code);
  DEF_BIT(2, imp_cal_en);
  DEF_BIT(1, hs_bias_en);
  DEF_BIT(0, intr_en);
  static auto Get() { return hwreg::RegisterAddr<MipiTxTopConReg>(MIPI_TX_TOP_CON); }
};

class MipiTxBgConReg : public hwreg::RegisterBase<MipiTxBgConReg, uint32_t> {
 public:
  DEF_FIELD(22, 20, v02_sel);
  DEF_FIELD(19, 17, v032_sel);
  DEF_FIELD(16, 14, v04_sel);
  DEF_FIELD(13, 11, v072_sel);
  DEF_FIELD(10, 8, v10_sel);
  DEF_FIELD(7, 5, v12_sel);
  DEF_BIT(1, bg_cken);
  DEF_BIT(0, bg_core_en);
  static auto Get() { return hwreg::RegisterAddr<MipiTxBgConReg>(MIPI_TX_BG_CON); }
};

class MipiTxPllCon0Reg : public hwreg::RegisterBase<MipiTxPllCon0Reg, uint32_t> {
 public:
  DEF_BIT(12, vod_en);
  DEF_BIT(11, monref_en);
  DEF_BIT(10, monvc_en);
  DEF_FIELD(9, 7, post_div);
  DEF_FIELD(6, 5, txdiv1);
  DEF_FIELD(4, 3, txdiv0);
  DEF_FIELD(2, 1, pre_div);
  DEF_BIT(0, pll_en);
  static auto Get() { return hwreg::RegisterAddr<MipiTxPllCon0Reg>(MIPI_TX_PLL_CON0); }
};

class MipiTxPllCon1Reg : public hwreg::RegisterBase<MipiTxPllCon1Reg, uint32_t> {
 public:
  DEF_FIELD(31, 16, sdm_ssc_prd);
  DEF_BIT(2, sdm_ssc_en);
  DEF_BIT(1, sdm_ssc_ph_init);
  DEF_BIT(0, sdm_fra_en);
  static auto Get() { return hwreg::RegisterAddr<MipiTxPllCon1Reg>(MIPI_TX_PLL_CON1); }
};

// This register's bit fields are not documented in datasheet
class MipiTxPllCon2Reg : public hwreg::RegisterBase<MipiTxPllCon2Reg, uint32_t> {
 public:
  DEF_FIELD(30, 24, pcw_h);
  DEF_FIELD(23, 16, pcw_23_16);
  DEF_FIELD(15, 8, pcw_15_8);
  DEF_FIELD(7, 0, pcw_7_0);
  static auto Get() { return hwreg::RegisterAddr<MipiTxPllCon2Reg>(MIPI_TX_PLL_CON2); }
};

class MipiTxPllCon3Reg : public hwreg::RegisterBase<MipiTxPllCon3Reg, uint32_t> {
 public:
  DEF_FIELD(31, 16, sdm_ssc_delta);
  DEF_FIELD(15, 0, sdm_ssc_delta1);
  static auto Get() { return hwreg::RegisterAddr<MipiTxPllCon3Reg>(MIPI_TX_PLL_CON3); }
};

class MipiTxPllChgReg : public hwreg::RegisterBase<MipiTxPllChgReg, uint32_t> {
 public:
  DEF_BIT(0, sdm_pcw_chg);
  static auto Get() { return hwreg::RegisterAddr<MipiTxPllChgReg>(MIPI_TX_PLL_CHG); }
};

class MipiTxPllTopReg : public hwreg::RegisterBase<MipiTxPllTopReg, uint32_t> {
 public:
  DEF_FIELD(15, 8, preserve);
  DEF_FIELD(3, 2, tstsel);
  DEF_BIT(1, tstck_en);
  DEF_BIT(0, tst_en);
  static auto Get() { return hwreg::RegisterAddr<MipiTxPllTopReg>(MIPI_TX_PLL_TOP); }
};

class MipiTxPllPwrReg : public hwreg::RegisterBase<MipiTxPllPwrReg, uint32_t> {
 public:
  DEF_BIT(8, sdm_pwr_ack);
  DEF_BIT(1, sdm_iso_en);
  DEF_BIT(0, sdm_pwr_on);
  static auto Get() { return hwreg::RegisterAddr<MipiTxPllPwrReg>(MIPI_TX_PLL_PWR); }
};

class MipiTxSwCtrlReg : public hwreg::RegisterBase<MipiTxSwCtrlReg, uint32_t> {
 public:
  DEF_BIT(0, enable);
  static auto Get() { return hwreg::RegisterAddr<MipiTxSwCtrlReg>(MIPI_TX_SW_CTRL); }
};

class MipiTxSwCtrlCon0Reg : public hwreg::RegisterBase<MipiTxSwCtrlCon0Reg, uint32_t> {
 public:
  DEF_FIELD(15, 8, sw_lnt0_hstx_data);
  DEF_BIT(7, sw_lnt0_lprx_en);
  DEF_BIT(6, sw_lntc_hstx_rdy);
  DEF_BIT(5, sw_lntc_hstx_oe);
  DEF_BIT(4, sw_lntc_hstx_pre_oe);
  DEF_BIT(3, sw_lntc_lptx_n);
  DEF_BIT(2, sw_lntc_lptx_p);
  DEF_BIT(1, sw_lntc_lptx_oe);
  DEF_BIT(0, sw_lntc_lptx_pre_oe);
  static auto Get() { return hwreg::RegisterAddr<MipiTxSwCtrlCon0Reg>(MIPI_TX_SW_CTRL_CON0); }
};

class MipiTxSwCtrlCon1Reg : public hwreg::RegisterBase<MipiTxSwCtrlCon1Reg, uint32_t> {
 public:
  DEF_BIT(31, sw_lnt3_lprx_en);
  DEF_BIT(30, sw_lnt3_hstx_rdy);
  DEF_BIT(29, sw_lnt3_hstx_oe);
  DEF_BIT(28, sw_lnt3_hstx_pre_oe);
  DEF_BIT(27, sw_lnt3_lptx_n);
  DEF_BIT(26, sw_lnt3_lptx_p);
  DEF_BIT(25, sw_lnt3_lptx_oe);
  DEF_BIT(24, sw_lnt3_lptx_pre_oe);
  DEF_BIT(23, sw_lnt2_lprx_en);
  DEF_BIT(22, sw_lnt2_hstx_rdy);
  DEF_BIT(21, sw_lnt2_hstx_oe);
  DEF_BIT(20, sw_lnt2_hstx_pre_oe);
  DEF_BIT(19, sw_lnt2_lptx_n);
  DEF_BIT(18, sw_lnt2_lptx_p);
  DEF_BIT(17, sw_lnt2_lptx_oe);
  DEF_BIT(16, sw_lnt2_lptx_pre_oe);
  DEF_BIT(15, sw_lnt1_lprx_en);
  DEF_BIT(14, sw_lnt1_hstx_rdy);
  DEF_BIT(13, sw_lnt1_hstx_oe);
  DEF_BIT(12, sw_lnt1_hstx_pre_oe);
  DEF_BIT(11, sw_lnt1_lptx_n);
  DEF_BIT(10, sw_lnt1_lptx_p);
  DEF_BIT(9, sw_lnt1_lptx_oe);
  DEF_BIT(8, sw_lnt1_lptx_pre_oe);
  DEF_BIT(7, sw_lnt0_lprx_en);
  DEF_BIT(6, sw_lnt0_hstx_rdy);
  DEF_BIT(5, sw_lnt0_hstx_oe);
  DEF_BIT(4, sw_lnt0_hstx_pre_oe);
  DEF_BIT(3, sw_lnt0_lptx_n);
  DEF_BIT(2, sw_lnt0_lptx_p);
  DEF_BIT(1, sw_lnt0_lptx_oe);
  DEF_BIT(0, sw_lnt0_lptx_pre_oe);
  static auto Get() { return hwreg::RegisterAddr<MipiTxSwCtrlCon1Reg>(MIPI_TX_SW_CTRL_CON1); }
};

}  // namespace mt8167s_display

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_MT8167S_DISPLAY_REGISTERS_MIPIPHY_H_
