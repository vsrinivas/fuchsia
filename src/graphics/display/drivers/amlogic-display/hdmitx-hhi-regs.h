// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_HDMITX_HHI_REGS_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_HDMITX_HHI_REGS_H_

#include <hwreg/bitfields.h>

#include "hhi-regs.h"

#define READ32_HHI_REG(a) hhi_mmio_->Read32(a)
#define WRITE32_HHI_REG(a, v) hhi_mmio_->Write32(v, a)

#define HHI_GCLK_MPEG2 (0x52 << 2)
#define HHI_VID_CLK_CNTL (0x5f << 2)
#define HHI_HDMI_CLK_CNTL (0x73 << 2)
#define HHI_VDAC_CNTL0_G12A (0xbb << 2)
#define HHI_VDAC_CNTL1_G12A (0xbc << 2)

#define HHI_HDMI_PLL_CNTL (0xc8 << 2)
// HHI_HDMI_PLL_CNTL bit definitions
#define PLL_CNTL_LOCK (1 << 31)
#define PLL_CNTL_ENABLE (1 << 30)
#define G12A_PLL_CNTL_RESET (1 << 29)
#define PLL_CNTL_RESET (1 << 28)
#define PLL_CNTL_N(x) (x << 9)
#define PLL_CNTL_M_START (0)
#define PLL_CNTL_M_BITS (9)

// HHI_HDMI_PLL_CNTL1 bit definitions
#define PLL_CNTL1_DIV_FRAC_START (0)
#define PLL_CNTL1_DIV_FRAC_BITS (12)

#define HHI_HDMI_PLL_STS (0xce << 2)

#define HHI_HDMI_PHY_CNTL0 (0xe8 << 2)
#define HHI_HDMI_PHY_CNTL1 (0xe9 << 2)
#define HHI_HDMI_PHY_CNTL2 (0xea << 2)
#define HHI_HDMI_PHY_CNTL3 (0xeb << 2)
#define HHI_HDMI_PHY_CNTL4 (0xec << 2)
#define HHI_HDMI_PHY_CNTL5 (0xed << 2)

namespace amlogic_display {

class HhiGclkMpeg2Reg : public hwreg::RegisterBase<HhiGclkMpeg2Reg, uint32_t> {
 public:
  // Undocumented
  DEF_BIT(4, clk81_en);

  static auto Get() { return hwreg::RegisterAddr<HhiGclkMpeg2Reg>(HHI_GCLK_MPEG2); }
};

class HhiMemPdReg0 : public hwreg::RegisterBase<HhiMemPdReg0, uint32_t> {
 public:
  // Memory PD
  DEF_FIELD(21, 20, axi_srame);
  DEF_FIELD(19, 18, apical_gdc);
  DEF_FIELD(15, 8, hdmi);
  DEF_FIELD(5, 4, audio);
  DEF_FIELD(3, 2, ethernet);

  static auto Get() { return hwreg::RegisterAddr<HhiMemPdReg0>(HHI_MEM_PD_REG0); }
};

class HhiVidClkCntlReg : public hwreg::RegisterBase<HhiVidClkCntlReg, uint32_t> {
 public:
  DEF_FIELD(31, 21, tcon_clk0_ctrl);
  DEF_BIT(20, clk_en1);
  DEF_BIT(19, clk_en0);
  DEF_FIELD(18, 16, clk_in_sel);
  DEF_BIT(15, soft_reset);
  DEF_BIT(14, ph23_enable);
  DEF_BIT(13, div12_ph23);
  DEF_BIT(4, div12_en);
  DEF_BIT(3, div6_en);
  DEF_BIT(2, div4_en);
  DEF_BIT(1, div2_en);
  DEF_BIT(0, div1_en);

  static auto Get() { return hwreg::RegisterAddr<HhiVidClkCntlReg>(HHI_VID_CLK_CNTL); }
};

class HhiVidClkCntl2Reg : public hwreg::RegisterBase<HhiVidClkCntl2Reg, uint32_t> {
 public:
  // Gated Clock Control
  DEF_BIT(8, atv_demod_vdac);
  DEF_BIT(7, lcd_an_clk_phy2);
  DEF_BIT(6, lcd_an_clk_phy3);
  DEF_BIT(5, hdmi_tx_pixel_clk);
  DEF_BIT(4, vdac_clk);
  DEF_BIT(3, encl);
  DEF_BIT(2, encp);
  DEF_BIT(1, enct);
  DEF_BIT(0, enci);

  static auto Get() { return hwreg::RegisterAddr<HhiVidClkCntl2Reg>(HHI_VID_CLK_CNTL2); }
};

class HhiVidClkDivReg : public hwreg::RegisterBase<HhiVidClkDivReg, uint32_t> {
 public:
  DEF_FIELD(31, 28, enci_clk_sel);
  DEF_FIELD(27, 24, encp_clk_sel);
  DEF_FIELD(23, 20, enct_clk_sel);
  DEF_BIT(17, clk_div_reset);
  DEF_BIT(16, clk_div_en);
  DEF_FIELD(15, 8, xd1);
  DEF_FIELD(7, 0, xd0);

  static auto Get() { return hwreg::RegisterAddr<HhiVidClkDivReg>(HHI_VID_CLK_DIV); }
};

class HhiVidPllClkDivReg : public hwreg::RegisterBase<HhiVidPllClkDivReg, uint32_t> {
 public:
  DEF_BIT(19, clk_final_en);
  DEF_BIT(18, clk_div1);
  DEF_FIELD(17, 16, clk_sel);
  DEF_BIT(15, set_preset);
  DEF_FIELD(14, 0, shift_preset);

  static auto Get() { return hwreg::RegisterAddr<HhiVidPllClkDivReg>(HHI_VID_PLL_CLK_DIV); }
};

class HhiHdmiClkCntlReg : public hwreg::RegisterBase<HhiHdmiClkCntlReg, uint32_t> {
 public:
  DEF_FIELD(19, 16, crt_hdmi_pixel_clk_sel);
  DEF_FIELD(10, 9, clk_sel);  // 0: oscin
                              // 1: fclk_div4
                              // 2: fclk_div3
                              // 3: fclk_div5
  DEF_BIT(8, clk_en);
  DEF_FIELD(6, 0, clk_div);

  static auto Get() { return hwreg::RegisterAddr<HhiHdmiClkCntlReg>(HHI_HDMI_CLK_CNTL); }
};

class HhiHdmiPllCntlReg : public hwreg::RegisterBase<HhiHdmiPllCntlReg, uint32_t> {
 public:
  DEF_BIT(31, hdmi_dpll_lock);
  DEF_BIT(30, hdmi_dpll_lock_a);
  DEF_BIT(29, hdmi_dpll_reset);
  DEF_BIT(28, hdmi_dpll_en);
  DEF_FIELD(25, 24, hdmi_dpll_out_gate_ctrl);
  DEF_FIELD(21, 20, hdmi_dpll_od3);
  DEF_FIELD(19, 18, hdmi_dpll_od2);
  DEF_FIELD(17, 16, hdmi_dpll_od1);
  DEF_FIELD(14, 10, hdmi_dpll_N);
  DEF_FIELD(7, 0, hdmi_dpll_M);

  static auto Get() { return hwreg::RegisterAddr<HhiHdmiPllCntlReg>(HHI_HDMI_PLL_CNTL); }
};

class HhiHdmiPllCntl1Reg : public hwreg::RegisterBase<HhiHdmiPllCntl1Reg, uint32_t> {
 public:
  DEF_FIELD(18, 0, hdmi_dpll_frac);

  static auto Get() { return hwreg::RegisterAddr<HhiHdmiPllCntl1Reg>(HHI_HDMI_PLL_CNTL1); }
};

class HhiHdmiPllCntl2Reg : public hwreg::RegisterBase<HhiHdmiPllCntl2Reg, uint32_t> {
 public:
  DEF_FIELD(22, 20, hdmi_dpll_fref_sel);
  DEF_FIELD(17, 16, hdmi_dpll_os_ssc);
  DEF_FIELD(15, 12, hdmi_dpll_ssc_str_m);
  DEF_BIT(8, hdmi_dpll_ssc_en);
  DEF_FIELD(7, 4, hdmi_dpll_ssc_dep_sel);
  DEF_FIELD(1, 0, hdmi_dpll_ss_mode);

  static auto Get() { return hwreg::RegisterAddr<HhiHdmiPllCntl2Reg>(HHI_HDMI_PLL_CNTL2); }
};

class HhiHdmiPllCntl3Reg : public hwreg::RegisterBase<HhiHdmiPllCntl3Reg, uint32_t> {
 public:
  DEF_BIT(31, hdmi_dpll_afc_bypass);
  DEF_BIT(30, hdmi_dpll_afc_clk_sel);
  DEF_BIT(29, hdmi_dpll_code_new);
  DEF_BIT(28, hdmi_dpll_dco_m_en);
  DEF_BIT(27, hdmi_dpll_dco_sdm_en);
  DEF_BIT(26, hdmi_dpll_div2);
  DEF_BIT(25, hdmi_dpll_div_mode);
  DEF_BIT(24, hdmi_dpll_fast_lock);
  DEF_BIT(23, hdmi_dpll_fb_pre_div);
  DEF_BIT(22, hdmi_dpll_filter_mode);
  DEF_BIT(21, hdmi_dpll_fix_en);
  DEF_BIT(20, hdmi_dpll_freq_shift_en);
  DEF_BIT(19, hdmi_dpll_load);
  DEF_BIT(18, hdmi_dpll_load_en);
  DEF_BIT(17, hdmi_dpll_lock_f);
  DEF_BIT(16, hdmi_dpll_pulse_width_en);
  DEF_BIT(15, hdmi_dpll_sdmnc_en);
  DEF_BIT(14, hdmi_dpll_sdmnc_mode);
  DEF_BIT(13, hdmi_dpll_sdmnc_range);
  DEF_BIT(12, hdmi_dpll_tdc_en);
  DEF_BIT(11, hdmi_dpll_tdc_mode_sel);
  DEF_BIT(10, hdmi_dpll_wait_en);

  static auto Get() { return hwreg::RegisterAddr<HhiHdmiPllCntl3Reg>(HHI_HDMI_PLL_CNTL3); }
};

class HhiHdmiPllCntl4Reg : public hwreg::RegisterBase<HhiHdmiPllCntl4Reg, uint32_t> {
 public:
  DEF_FIELD(30, 28, hdmi_dpll_alpha);
  DEF_FIELD(26, 24, hdmi_dpll_rou);
  DEF_FIELD(22, 20, hdmi_dpll_lambda1);
  DEF_FIELD(18, 16, hdmi_dpll_lambda0);
  DEF_FIELD(13, 12, hdmi_dpll_acq_gain);
  DEF_FIELD(11, 8, hdmi_dpll_filter_pvt2);
  DEF_FIELD(7, 4, hdmi_dpll_filter_pvt1);
  DEF_FIELD(1, 0, hdmi_dpll_pfd_gain);

  static auto Get() { return hwreg::RegisterAddr<HhiHdmiPllCntl4Reg>(HHI_HDMI_PLL_CNTL4); }
};

class HhiHdmiPllCntl5Reg : public hwreg::RegisterBase<HhiHdmiPllCntl5Reg, uint32_t> {
 public:
  DEF_FIELD(30, 28, hdmi_dpll_adj_vco_ldo);
  DEF_FIELD(27, 24, hdmi_dpll_lm_w);
  DEF_FIELD(21, 16, hdmi_dpll_lm_s);
  DEF_FIELD(15, 0, hdmi_dpll_reve);

  static auto Get() { return hwreg::RegisterAddr<HhiHdmiPllCntl5Reg>(HHI_HDMI_PLL_CNTL5); }
};

class HhiHdmiPllCntl6Reg : public hwreg::RegisterBase<HhiHdmiPllCntl6Reg, uint32_t> {
 public:
  DEF_FIELD(31, 30, hdmi_dpll_afc_hold_t);
  DEF_FIELD(29, 28, hdmi_dpll_lkw_sel);
  DEF_FIELD(27, 26, hdmi_dpll_dco_sdm_clk_sel);
  DEF_FIELD(25, 24, hdmi_dpll_afc_in);
  DEF_FIELD(23, 22, hdmi_dpll_afc_nt);
  DEF_FIELD(21, 20, hdmi_dpll_vc_in);
  DEF_FIELD(19, 18, hdmi_dpll_lock_long);
  DEF_FIELD(17, 16, hdmi_dpll_freq_shift_v);
  DEF_FIELD(14, 12, hdmi_dpll_data_sel);
  DEF_FIELD(10, 8, hdmi_dpll_sdmnc_ulms);
  DEF_FIELD(6, 0, hdmi_dpll_sdmnc_power);

  static auto Get() { return hwreg::RegisterAddr<HhiHdmiPllCntl6Reg>(HHI_HDMI_PLL_CNTL6); }
};

class HhiHdmiPllStsReg : public hwreg::RegisterBase<HhiHdmiPllStsReg, uint32_t> {
 public:
  DEF_BIT(31, hdmi_dpll_lock);
  DEF_BIT(30, hdmi_dpll_lock_a);
  DEF_BIT(29, hdmi_dpll_afc_done);
  DEF_FIELD(22, 16, hdmi_dpll_sdmnc_monitor);
  DEF_FIELD(9, 0, hdmi_dpll_out_rsv);

  static auto Get() { return hwreg::RegisterAddr<HhiHdmiPllStsReg>(HHI_HDMI_PLL_STS); }
};

class HhiHdmiPhyCntl0Reg : public hwreg::RegisterBase<HhiHdmiPhyCntl0Reg, uint32_t> {
 public:
  DEF_FIELD(31, 16, hdmi_ctl1);
  DEF_FIELD(15, 0, hdmi_ctl2);

  static auto Get() { return hwreg::RegisterAddr<HhiHdmiPhyCntl0Reg>(HHI_HDMI_PHY_CNTL0); }
};

class HhiHdmiPhyCntl1Reg : public hwreg::RegisterBase<HhiHdmiPhyCntl1Reg, uint32_t> {
 public:
  DEF_FIELD(31, 30, new_prbs_mode);
  DEF_FIELD(29, 28, new_prbs_prbsmode);
  DEF_BIT(27, new_prbs_sel);
  DEF_BIT(26, new_prbs_en);
  DEF_FIELD(25, 24, ch3_swap);  // 0: ch0, 1: ch1, 2: ch2, 3: ch3
  DEF_FIELD(23, 22, ch2_swap);  // 0: ch0, 1: ch1, 2: ch2, 3: ch3
  DEF_FIELD(21, 20, ch1_swap);  // 0: ch0, 1: ch1, 2: ch2, 3: ch3
  DEF_FIELD(19, 18, ch0_swap);  // 0: ch0, 1: ch1, 2: ch2, 3: ch3
  DEF_BIT(17, bit_invert);
  DEF_BIT(16, msb_lsb_swap);
  DEF_BIT(15, capture_add1);
  DEF_BIT(14, capture_clk_gate_en);
  DEF_BIT(13, hdmi_tx_prbs_en);
  DEF_BIT(12, hdmi_tx_prbs_err_en);
  DEF_FIELD(11, 8, hdmi_tx_sel_high);
  DEF_FIELD(7, 4, hdmi_tx_sel_low);
  DEF_BIT(3, hdmi_fifo_wr_enable);
  DEF_BIT(2, hdmi_fifo_enable);
  DEF_BIT(1, hdmi_tx_phy_clk_en);
  DEF_BIT(0, hdmi_tx_phy_soft_reset);

  static auto Get() { return hwreg::RegisterAddr<HhiHdmiPhyCntl1Reg>(HHI_HDMI_PHY_CNTL1); }
};

class HhiHdmiPhyCntl3Reg : public hwreg::RegisterBase<HhiHdmiPhyCntl3Reg, uint32_t> {
 public:
  // Undocumented

  static auto Get() { return hwreg::RegisterAddr<HhiHdmiPhyCntl3Reg>(HHI_HDMI_PHY_CNTL3); }
};

class HhiHdmiPhyCntl5Reg : public hwreg::RegisterBase<HhiHdmiPhyCntl5Reg, uint32_t> {
 public:
  DEF_FIELD(31, 24, new_pbrs_err_thr);
  DEF_FIELD(21, 20, dtest_sel);
  DEF_BIT(19, new_prbs_clr_ber_meter);
  DEF_BIT(17, new_prbs_freez_ber);
  DEF_BIT(16, new_prbs_inverse_in);
  DEF_FIELD(15, 14, new_prbs_mode);

  static auto Get() { return hwreg::RegisterAddr<HhiHdmiPhyCntl5Reg>(HHI_HDMI_PHY_CNTL5); }
};

}  // namespace amlogic_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_HDMITX_HHI_REGS_H_
