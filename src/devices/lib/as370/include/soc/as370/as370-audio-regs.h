// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_AUDIO_REGS_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_AUDIO_REGS_H_

#include <zircon/types.h>

#include <hwreg/bitfields.h>

// AIO I2S Registers.

struct AIO_PRI_PRIAUD_CLKDIV : public hwreg::RegisterBase<AIO_PRI_PRIAUD_CLKDIV, uint32_t> {
  DEF_FIELD(3, 0, SETTING);
  static auto Get() { return hwreg::RegisterAddr<AIO_PRI_PRIAUD_CLKDIV>(0x0000); }
};

struct AIO_PRI_PRIAUD_CTRL : public hwreg::RegisterBase<AIO_PRI_PRIAUD_CTRL, uint32_t> {
  DEF_FIELD(31, 24, TCF_MANUAL);
  DEF_FIELD(23, 16, TDMWSHIGH);
  DEF_FIELD(15, 13, TDMCHCNT);
  DEF_BIT(12, TDMMODE);
  DEF_FIELD(11, 10, TFM);
  DEF_FIELD(9, 7, TCF);
  DEF_FIELD(6, 4, TDM);
  DEF_BIT(3, TLSB);
  DEF_BIT(2, INVFS);
  DEF_BIT(1, INVCLK);
  DEF_BIT(0, LEFTJFY);
  static auto Get() { return hwreg::RegisterAddr<AIO_PRI_PRIAUD_CTRL>(0x0004); }
};

struct AIO_PRI_PRIAUD_CTRL1 : public hwreg::RegisterBase<AIO_PRI_PRIAUD_CTRL1, uint32_t> {
  DEF_BIT(11, PCM_MONO_CH);
  DEF_FIELD(10, 3, TDM_MANUAL);
  DEF_FIELD(2, 0, TCF_MAN_MAR);
  static auto Get() { return hwreg::RegisterAddr<AIO_PRI_PRIAUD_CTRL1>(0x0008); }
};

struct AIO_PRI_TSD0_PRI_CTRL : public hwreg::RegisterBase<AIO_PRI_TSD0_PRI_CTRL, uint32_t> {
  DEF_BIT(4, FLUSH);
  DEF_BIT(2, LRSWITCH);
  DEF_BIT(1, MUTE);
  DEF_BIT(0, ENABLE);
  static auto Get() { return hwreg::RegisterAddr<AIO_PRI_TSD0_PRI_CTRL>(0x000c); }
};

struct AIO_PRI_TSD1_PRI_CTRL : public hwreg::RegisterBase<AIO_PRI_TSD1_PRI_CTRL, uint32_t> {
  DEF_BIT(4, FLUSH);
  DEF_BIT(2, LRSWITCH);
  DEF_BIT(1, MUTE);
  DEF_BIT(0, ENABLE);
  static auto Get() { return hwreg::RegisterAddr<AIO_PRI_TSD1_PRI_CTRL>(0x0010); }
};

struct AIO_PRI_TSD2_PRI_CTRL : public hwreg::RegisterBase<AIO_PRI_TSD2_PRI_CTRL, uint32_t> {
  DEF_BIT(4, FLUSH);
  DEF_BIT(2, LRSWITCH);
  DEF_BIT(1, MUTE);
  DEF_BIT(0, ENABLE);
  static auto Get() { return hwreg::RegisterAddr<AIO_PRI_TSD2_PRI_CTRL>(0x0014); }
};

struct AIO_PRI_TSD3_PRI_CTRL : public hwreg::RegisterBase<AIO_PRI_TSD3_PRI_CTRL, uint32_t> {
  DEF_BIT(4, FLUSH);
  DEF_BIT(2, LRSWITCH);
  DEF_BIT(1, MUTE);
  DEF_BIT(0, ENABLE);
  static auto Get() { return hwreg::RegisterAddr<AIO_PRI_TSD3_PRI_CTRL>(0x0018); }
};

struct AIO_PRI_PRIPORT : public hwreg::RegisterBase<AIO_PRI_PRIPORT, uint32_t> {
  DEF_BIT(0, ENABLE);
  static auto Get() { return hwreg::RegisterAddr<AIO_PRI_PRIPORT>(0x0024); }
};

struct AIO_IOSEL_PRIBCLK : public hwreg::RegisterBase<AIO_IOSEL_PRIBCLK, uint32_t> {
  DEF_BIT(2, INV);
  DEF_FIELD(1, 0, SEL);
  static auto Get() { return hwreg::RegisterAddr<AIO_IOSEL_PRIBCLK>(0x012c); }
};

struct AIO_IRQENABLE : public hwreg::RegisterBase<AIO_IRQENABLE, uint32_t> {
  DEF_BIT(6, SPDIFRXIRQ);
  DEF_BIT(5, PDMIRQ);
  DEF_BIT(4, SPDIFIRQ);
  DEF_BIT(3, MIC2IRQ);
  DEF_BIT(2, MIC1IRQ);
  DEF_BIT(1, SECIRQ);
  DEF_BIT(0, PRIIRQ);
  static auto Get() { return hwreg::RegisterAddr<AIO_IRQENABLE>(0x0150); }
};

struct AIO_IRQSTS : public hwreg::RegisterBase<AIO_IRQSTS, uint32_t> {
  DEF_BIT(6, SPDIFRXSTS);
  DEF_BIT(5, PDMSTS);
  DEF_BIT(4, SPDIFSTS);
  DEF_BIT(3, MIC2STS);
  DEF_BIT(2, MIC1STS);
  DEF_BIT(1, SECSTS);
  DEF_BIT(0, PRISTS);
  static auto Get() { return hwreg::RegisterAddr<AIO_IRQSTS>(0x0150); }
};

struct AIO_MCLKPRI_ACLK_CTRL : public hwreg::RegisterBase<AIO_MCLKPRI_ACLK_CTRL, uint32_t> {
  DEF_BIT(8, sw_sync_rst);
  DEF_FIELD(7, 5, clkSel);
  DEF_BIT(4, clkD3Switch);
  DEF_BIT(3, clkSwitch);
  DEF_FIELD(2, 1, src_sel);
  DEF_BIT(0, clk_Enable);
  static auto Get() { return hwreg::RegisterAddr<AIO_MCLKPRI_ACLK_CTRL>(0x0164); }
};

struct AIO_MCLKSEC_ACLK_CTRL : public hwreg::RegisterBase<AIO_MCLKSEC_ACLK_CTRL, uint32_t> {
  DEF_BIT(8, sw_sync_rst);
  DEF_FIELD(7, 5, clkSel);
  DEF_BIT(4, clkD3Switch);
  DEF_BIT(3, clkSwitch);
  DEF_FIELD(2, 1, src_sel);
  DEF_BIT(0, clk_Enable);
  static auto Get() { return hwreg::RegisterAddr<AIO_MCLKSEC_ACLK_CTRL>(0x0168); }
};

struct AIO_MCLKSPF_ACLK_CTRL : public hwreg::RegisterBase<AIO_MCLKSPF_ACLK_CTRL, uint32_t> {
  DEF_BIT(8, sw_sync_rst);
  DEF_FIELD(7, 5, clkSel);
  DEF_BIT(4, clkD3Switch);
  DEF_BIT(3, clkSwitch);
  DEF_FIELD(2, 1, src_sel);
  DEF_BIT(0, clk_Enable);
  static auto Get() { return hwreg::RegisterAddr<AIO_MCLKSPF_ACLK_CTRL>(0x016c); }
};

struct AIO_MCLKPDM_ACLK_CTRL : public hwreg::RegisterBase<AIO_MCLKPDM_ACLK_CTRL, uint32_t> {
  DEF_BIT(8, sw_sync_rst);
  DEF_FIELD(7, 5, clkSel);
  DEF_BIT(4, clkD3Switch);
  DEF_BIT(3, clkSwitch);
  DEF_FIELD(2, 1, src_sel);
  DEF_BIT(0, clk_Enable);
  static auto Get() { return hwreg::RegisterAddr<AIO_MCLKPDM_ACLK_CTRL>(0x0170); }
};

struct AIO_MCLKMIC1_ACLK_CTRL : public hwreg::RegisterBase<AIO_MCLKMIC1_ACLK_CTRL, uint32_t> {
  DEF_BIT(8, sw_sync_rst);
  DEF_FIELD(7, 5, clkSel);
  DEF_BIT(4, clkD3Switch);
  DEF_BIT(3, clkSwitch);
  DEF_FIELD(2, 1, src_sel);
  DEF_BIT(0, clk_Enable);
  static auto Get() { return hwreg::RegisterAddr<AIO_MCLKMIC1_ACLK_CTRL>(0x0174); }
};

struct AIO_MCLKMIC2_ACLK_CTRL : public hwreg::RegisterBase<AIO_MCLKMIC2_ACLK_CTRL, uint32_t> {
  DEF_BIT(8, sw_sync_rst);
  DEF_FIELD(7, 5, clkSel);
  DEF_BIT(4, clkD3Switch);
  DEF_BIT(3, clkSwitch);
  DEF_FIELD(2, 1, src_sel);
  DEF_BIT(0, clk_Enable);
  static auto Get() { return hwreg::RegisterAddr<AIO_MCLKMIC2_ACLK_CTRL>(0x0178); }
};

// PDM Registers (under I2S, but not really I2S).

struct AIO_PDM_CTRL1 : public hwreg::RegisterBase<AIO_PDM_CTRL1, uint32_t> {
  DEF_BIT(12, LATCH_MODE);
  DEF_BIT(11, SDR_CLKSEL);
  DEF_BIT(10, MODE);
  DEF_FIELD(9, 7, RDM);
  DEF_BIT(6, RSLB);
  DEF_BIT(5, INVCLK_INT);
  DEF_BIT(4, INVCLK_OUT);
  DEF_FIELD(3, 0, CLKDIV);
  static auto Get() { return hwreg::RegisterAddr<AIO_PDM_CTRL1>(0x00c0); }
};

struct AIO_PDM_PDM0_CTRL : public hwreg::RegisterBase<AIO_PDM_PDM0_CTRL, uint32_t> {
  DEF_BIT(3, FLUSH);
  DEF_BIT(2, LRSWITCH);
  DEF_BIT(1, MUTE);
  DEF_BIT(0, ENABLE);
  static auto Get() { return hwreg::RegisterAddr<AIO_PDM_PDM0_CTRL>(0x00d0); }
};

struct AIO_PDM_PDM0_CTRL2 : public hwreg::RegisterBase<AIO_PDM_PDM0_CTRL2, uint32_t> {
  DEF_FIELD(31, 16, FDLT);
  DEF_FIELD(15, 0, RDLT);
  static auto Get() { return hwreg::RegisterAddr<AIO_PDM_PDM0_CTRL2>(0x00d4); }
};

struct AIO_PDM_PDM1_CTRL : public hwreg::RegisterBase<AIO_PDM_PDM1_CTRL, uint32_t> {
  DEF_BIT(3, FLUSH);
  DEF_BIT(2, LRSWITCH);
  DEF_BIT(1, MUTE);
  DEF_BIT(0, ENABLE);
  static auto Get() { return hwreg::RegisterAddr<AIO_PDM_PDM1_CTRL>(0x00d8); }
};

struct AIO_PDM_PDM1_CTRL2 : public hwreg::RegisterBase<AIO_PDM_PDM1_CTRL2, uint32_t> {
  DEF_FIELD(31, 16, FDLT);
  DEF_FIELD(15, 0, RDLT);
  static auto Get() { return hwreg::RegisterAddr<AIO_PDM_PDM1_CTRL2>(0x00dc); }
};

struct AIO_PDM_PDM2_CTRL : public hwreg::RegisterBase<AIO_PDM_PDM2_CTRL, uint32_t> {
  DEF_BIT(3, FLUSH);
  DEF_BIT(2, LRSWITCH);
  DEF_BIT(1, MUTE);
  DEF_BIT(0, ENABLE);
  static auto Get() { return hwreg::RegisterAddr<AIO_PDM_PDM2_CTRL>(0x00e0); }
};

struct AIO_PDM_PDM2_CTRL2 : public hwreg::RegisterBase<AIO_PDM_PDM2_CTRL2, uint32_t> {
  DEF_FIELD(31, 16, FDLT);
  DEF_FIELD(15, 0, RDLT);
  static auto Get() { return hwreg::RegisterAddr<AIO_PDM_PDM2_CTRL2>(0x00e4); }
};

struct AIO_PDM_PDM3_CTRL : public hwreg::RegisterBase<AIO_PDM_PDM3_CTRL, uint32_t> {
  DEF_BIT(3, FLUSH);
  DEF_BIT(2, LRSWITCH);
  DEF_BIT(1, MUTE);
  DEF_BIT(0, ENABLE);
  static auto Get() { return hwreg::RegisterAddr<AIO_PDM_PDM3_CTRL>(0x00e8); }
};

struct AIO_PDM_PDM3_CTRL2 : public hwreg::RegisterBase<AIO_PDM_PDM3_CTRL2, uint32_t> {
  DEF_FIELD(31, 16, FDLT);
  DEF_FIELD(15, 0, RDLT);
  static auto Get() { return hwreg::RegisterAddr<AIO_PDM_PDM3_CTRL2>(0x00ec); }
};

struct AIO_IOSEL_PDM : public hwreg::RegisterBase<AIO_IOSEL_PDM, uint32_t> {
  DEF_BIT(0, GENABLE);
  static auto Get() { return hwreg::RegisterAddr<AIO_IOSEL_PDM>(0x014c); }
};

struct AIO_PDM_MIC_SEL : public hwreg::RegisterBase<AIO_PDM_MIC_SEL, uint32_t> {
  DEF_FIELD(3, 0, CTRL);
  static auto Get() { return hwreg::RegisterAddr<AIO_PDM_MIC_SEL>(0x0160); }
};

// AVIO Global Registers.

struct avioGbl_AVPLL0_WRAP_CTRL0 : public hwreg::RegisterBase<avioGbl_AVPLL0_WRAP_CTRL0, uint32_t> {
  DEF_BIT(6, clkOut_sel);
  DEF_BIT(5, clk_sel1);
  static auto Get() { return hwreg::RegisterAddr<avioGbl_AVPLL0_WRAP_CTRL0>(0x0004); }
};

struct avioGbl_AVPLL1_WRAP_CTRL0 : public hwreg::RegisterBase<avioGbl_AVPLL1_WRAP_CTRL0, uint32_t> {
  DEF_BIT(6, clkOut_sel);
  DEF_BIT(5, clk_sel1);
  DEF_FIELD(4, 3, clk_sel0);
  DEF_FIELD(2, 1, I2S_BCLKI_SEL);
  static auto Get() { return hwreg::RegisterAddr<avioGbl_AVPLL1_WRAP_CTRL0>(0x0024); }
};

struct avioGbl_AVPLLx_WRAP_AVPLL_CLK1_CTRL
    : public hwreg::RegisterBase<avioGbl_AVPLLx_WRAP_AVPLL_CLK1_CTRL, uint32_t> {
  DEF_BIT(5, clkEn);
  DEF_FIELD(4, 2, clkSel);
  DEF_BIT(1, clkD3Switch);
  DEF_BIT(0, clkSwitch);
  static auto Get(uint32_t id) {
    return hwreg::RegisterAddr<avioGbl_AVPLLx_WRAP_AVPLL_CLK1_CTRL>(id * 0x20 + 0x0000);
  }
};

struct avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl
    : public hwreg::RegisterBase<avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl, uint32_t> {
  DEF_BIT(22, FRAC_READY);
  DEF_BIT(21, READY);
  DEF_FIELD(20, 19, MODE);
  DEF_FIELD(18, 8, DN);
  DEF_FIELD(7, 2, DM);
  DEF_BIT(1, RESETN);
  DEF_BIT(0, PD);
  static auto Get(uint32_t id) {
    return hwreg::RegisterAddr<avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl>(id * 0x20 + 0x0008);
  }
};

struct avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl1
    : public hwreg::RegisterBase<avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl1, uint32_t> {
  DEF_FIELD(23, 0, FRAC);
  static auto Get(uint32_t id) {
    return hwreg::RegisterAddr<avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl1>(id * 0x20 + 0x000c);
  }
};

struct avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl2
    : public hwreg::RegisterBase<avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl2, uint32_t> {
  DEF_FIELD(10, 0, SSRATE);
  static auto Get(uint32_t id) {
    return hwreg::RegisterAddr<avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl2>(id * 0x20 + 0x0010);
  }
};

struct avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl3
    : public hwreg::RegisterBase<avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl3, uint32_t> {
  DEF_FIELD(31, 29, DP1);
  DEF_BIT(28, PDDP1);
  DEF_FIELD(27, 25, DP);
  DEF_BIT(24, PDDP);
  DEF_FIELD(23, 0, SLOPE);
  static auto Get(uint32_t id) {
    return hwreg::RegisterAddr<avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl3>(id * 0x20 + 0x0014);
  }
};

struct avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl4
    : public hwreg::RegisterBase<avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl4, uint32_t> {
  DEF_BIT(0, BYPASS);
  static auto Get(uint32_t id) {
    return hwreg::RegisterAddr<avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl4>(id * 0x20 + 0x0018);
  }
};

struct avioGbl_AVPLLx_WRAP_AVPLL_vsipll_status
    : public hwreg::RegisterBase<avioGbl_AVPLLx_WRAP_AVPLL_vsipll_status, uint32_t> {
  DEF_BIT(0, LOCK);
  static auto Get(uint32_t id) {
    return hwreg::RegisterAddr<avioGbl_AVPLLx_WRAP_AVPLL_vsipll_status>(id * 0x20 + 0x001c);
  }
};

struct avioGbl_AVPLLA_CLK_EN : public hwreg::RegisterBase<avioGbl_AVPLLA_CLK_EN, uint32_t> {
  DEF_BIT(3, ctrl_AVPLL1);
  DEF_BIT(2, ctrl_AVPLL0);
  static auto Get() { return hwreg::RegisterAddr<avioGbl_AVPLLA_CLK_EN>(0x0044); }
};

struct avioGbl_SWRST_CTRL : public hwreg::RegisterBase<avioGbl_SWRST_CTRL, uint32_t> {
  DEF_BIT(2, avpll1Rstn);
  DEF_BIT(1, avpll0Rstn);
  DEF_BIT(0, biuSyncRstn);
  static auto Get() { return hwreg::RegisterAddr<avioGbl_SWRST_CTRL>(0x0048); }
};

struct avioGbl_SWPDWN_CTRL : public hwreg::RegisterBase<avioGbl_SWPDWN_CTRL, uint32_t> {
  DEF_BIT(1, APLL1_PD);
  DEF_BIT(0, APLL0_PD);
  static auto Get() { return hwreg::RegisterAddr<avioGbl_SWPDWN_CTRL>(0x004c); }
};

struct avioGbl_CTRL : public hwreg::RegisterBase<avioGbl_CTRL, uint32_t> {
  DEF_FIELD(6, 3, INTR_EN);
  DEF_BIT(2, AIODHUB_CG_en);
  DEF_BIT(1, AIODHUB_swCG_en);
  DEF_BIT(0, AIODHUB_dyCG_en);
  static auto Get() { return hwreg::RegisterAddr<avioGbl_CTRL>(0x0058); }
};

struct avioGbl_CTRL0 : public hwreg::RegisterBase<avioGbl_CTRL0, uint32_t> {
  DEF_BIT(2, I2S3_LRCLK_OEN);
  DEF_BIT(1, I2S3_BCLK_OEN);
  DEF_BIT(0, I2S1_MCLK_OEN);
  static auto Get() { return hwreg::RegisterAddr<avioGbl_CTRL0>(0x005c); }
};

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_AUDIO_REGS_H_
