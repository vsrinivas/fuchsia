// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_CLK_REGS_H_
#define SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_CLK_REGS_H_

#include <zircon/types.h>

#include <hwreg/bitfields.h>
#include <soc/mt8167/mt8167-hw.h>

// XO.
// Clock Mux Selection 0.
class CLK_MUX_SEL0 : public hwreg::RegisterBase<CLK_MUX_SEL0, uint32_t> {
 public:
  static constexpr uint32_t kMsdc0ClkMmPllDiv3 = 5;
  static constexpr uint32_t kMsdc0ClkMmPllDiv2 = 7;

  DEF_FIELD(29, 27, rg_aud_intbus_sel);
  DEF_BIT(26, rg_aud_hf_26m_sel);
  DEF_FIELD(13, 11, msdc0_mux_sel);
  static auto Get() { return hwreg::RegisterAddr<CLK_MUX_SEL0>(0x000); }
};

// Clock Gating Control 1 Security Enable.
class CLK_GATING_CTRL1_SEN : public hwreg::RegisterBase<CLK_GATING_CTRL1_SEN, uint32_t> {
 public:
  DEF_BIT(25, rg_audio_sw_cg);
  static auto Get() { return hwreg::RegisterAddr<CLK_GATING_CTRL1_SEN>(0x224); }
};

// Audio Clock Selection Register.
class CLK_SEL_9 : public hwreg::RegisterBase<CLK_SEL_9, uint32_t> {
 public:
  DEF_BIT(17, apll_i2s5_mck_sel);
  DEF_BIT(16, apll_i2s4_mck_sel);
  DEF_BIT(15, apll_i2s3_mck_sel);
  DEF_BIT(14, apll_i2s2_mck_sel);
  DEF_BIT(13, apll_i2s1_mck_sel);
  DEF_BIT(12, apll_i2s0_mck_sel);
  DEF_BIT(8, apll12_div6_pdn);
  DEF_BIT(7, apll12_div5b_pdn);
  DEF_BIT(6, apll12_div5_pdn);
  DEF_BIT(5, apll12_div4b_pdn);
  DEF_BIT(4, apll12_div4_pdn);
  DEF_BIT(3, apll12_div3_pdn);
  DEF_BIT(2, apll12_div2_pdn);
  DEF_BIT(1, apll12_div1_pdn);
  DEF_BIT(0, apll12_div0_pdn);
  static auto Get() { return hwreg::RegisterAddr<CLK_SEL_9>(0x044); }
};

// Audio Clock Selection Register 1.
class CLK_SEL_10 : public hwreg::RegisterBase<CLK_SEL_10, uint32_t> {
 public:
  DEF_FIELD(31, 24, apll12_ck_div3);
  DEF_FIELD(23, 16, apll12_ck_div2);
  DEF_FIELD(15, 8, apll12_ck_div1);
  DEF_FIELD(7, 0, apll12_ck_div0);
  static auto Get() { return hwreg::RegisterAddr<CLK_SEL_10>(0x048); }
};

// Audio Clock Selection Register 2.
class CLK_SEL_11 : public hwreg::RegisterBase<CLK_SEL_11, uint32_t> {
 public:
  DEF_FIELD(31, 24, apll12_ck_div5b);
  DEF_FIELD(23, 16, apll12_ck_div5);
  DEF_FIELD(15, 8, apll12_ck_div4b);
  DEF_FIELD(7, 0, apll12_ck_div4);
  static auto Get() { return hwreg::RegisterAddr<CLK_SEL_11>(0x04C); }
};

// Set Clock Gating Control 1.
class SET_CLK_GATING_CTRL1 : public hwreg::RegisterBase<SET_CLK_GATING_CTRL1, uint32_t> {
 public:
  DEF_BIT(25, rg_audio_sw_cg);
  static auto Get() { return hwreg::RegisterAddr<SET_CLK_GATING_CTRL1>(0x054); }
};

// Clear Clock Gating Control 8.
class CLR_CLK_GATING_CTRL8 : public hwreg::RegisterBase<CLR_CLK_GATING_CTRL8, uint32_t> {
 public:
  DEF_BIT(11, rq_aud_engen2_sw_cg);
  DEF_BIT(10, rq_aud_engen1_sw_cg);
  DEF_BIT(9, rq_aud2_sw_cg);
  DEF_BIT(8, rq_aud1_sw_cg);
  static auto Get() { return hwreg::RegisterAddr<CLR_CLK_GATING_CTRL8>(0x0B0); }
};

// Set Clock Gating Control 8.
class SET_CLK_GATING_CTRL8 : public hwreg::RegisterBase<SET_CLK_GATING_CTRL8, uint32_t> {
 public:
  DEF_BIT(11, rq_aud_engen2_sw_cg);
  DEF_BIT(10, rq_aud_engen1_sw_cg);
  DEF_BIT(9, rq_aud2_sw_cg);
  DEF_BIT(8, rq_aud1_sw_cg);
  static auto Get() { return hwreg::RegisterAddr<SET_CLK_GATING_CTRL8>(0x0A0); }
};

// PLL
// AUD1PLL Control Register 0.
class APLL1_CON0 : public hwreg::RegisterBase<APLL1_CON0, uint32_t> {
 public:
  DEF_BIT(4, APLL1_SDM_FRA_EN);
  DEF_FIELD(3, 1, APLL1_POSDIV);
  DEF_BIT(0, APLL1_EN);
  static auto Get() { return hwreg::RegisterAddr<APLL1_CON0>(0x180); }
};

// AUD2PLL Control Register 0.
class APLL2_CON0 : public hwreg::RegisterBase<APLL2_CON0, uint32_t> {
 public:
  DEF_BIT(0, APLL2_EN);
  static auto Get() { return hwreg::RegisterAddr<APLL2_CON0>(0x1A0); }
};

class MmPllCon1 : public hwreg::RegisterBase<MmPllCon1, uint32_t> {
 public:
  static constexpr uint32_t kDiv1 = 0;
  static constexpr uint32_t kDiv2 = 1;
  static constexpr uint32_t kDiv4 = 2;
  static constexpr uint32_t kDiv8 = 3;
  static constexpr uint32_t kDiv16 = 4;

  static constexpr uint32_t kPcwFracBits = 14;

  static auto Get() { return hwreg::RegisterAddr<MmPllCon1>(0x164); }

  DEF_BIT(31, change);
  DEF_FIELD(26, 24, div);
  DEF_FIELD(20, 0, pcw);
};

#endif  // SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_CLK_REGS_H_
