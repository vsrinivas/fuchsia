// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_AUDIO_REGS_H_
#define SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_AUDIO_REGS_H_

#include <zircon/types.h>

#include <hwreg/bitfields.h>
#include <soc/mt8167/mt8167-hw.h>

// Partial Glossary:
// AFE Audio Front End.
// CG Clock Gating.
// DL Downlink.
// PDN Power Down.
// VUL Voice Uplink.

// Top Control Register 0.
class AUDIO_TOP_CON0 : public hwreg::RegisterBase<AUDIO_TOP_CON0, uint32_t> {
 public:
  DEF_BIT(20, PDN_HDMI_CK);
  DEF_BIT(5, PDN_I2S);
  DEF_BIT(2, PDN_AFE);
  static auto Get() { return hwreg::RegisterAddr<AUDIO_TOP_CON0>(0x000); }
};

// Top Control Register 1.
class AUDIO_TOP_CON1 : public hwreg::RegisterBase<AUDIO_TOP_CON1, uint32_t> {
 public:
  DEF_BIT(7, I2S4_BCLK_SW_CG);
  DEF_BIT(6, I2S3_BCLK_SW_CG);
  DEF_BIT(5, I2S2_BCLK_SW_CG);
  DEF_BIT(4, I2S1_BCLK_SW_CG);
  DEF_BIT(2, I2S_SOFT_RST2);
  DEF_BIT(1, I2S_SOFT_RST);
  static auto Get() { return hwreg::RegisterAddr<AUDIO_TOP_CON1>(0x004); }
};

// AFE Control Register 0.
class AFE_DAC_CON0 : public hwreg::RegisterBase<AFE_DAC_CON0, uint32_t> {
 public:
  DEF_BIT(26, MOD_PCM_DUP_WR);
  DEF_FIELD(25, 24, DAI_MODE);
  DEF_BIT(7, MOD_DAI_ON);
  DEF_BIT(6, AWB_ON);
  DEF_BIT(4, DAI_ON);
  DEF_BIT(3, VUL_ON);
  DEF_BIT(2, DL2_ON);
  DEF_BIT(1, DL1_ON);
  DEF_BIT(0, AFE_ON);
  static auto Get() { return hwreg::RegisterAddr<AFE_DAC_CON0>(0x010); }
};

// AFE Control Register 1.
class AFE_DAC_CON1 : public hwreg::RegisterBase<AFE_DAC_CON1, uint32_t> {
 public:
  DEF_FIELD(31, 30, MOD_PCM_MODE);
  DEF_BIT(29, DAI_DUP_WR);
  DEF_BIT(28, VUL_R_MONO);
  DEF_BIT(27, VUL_DATA);
  DEF_BIT(26, AXI_2X1_CG_DISABLE);
  DEF_BIT(25, AWB_R_MONO);
  DEF_BIT(24, AWB_DATA);
  DEF_BIT(22, DL2_DATA);
  DEF_BIT(21, DL1_DATA);
  DEF_FIELD(19, 16, VUL_MODE);
  DEF_FIELD(15, 12, AWB_MODE);
  DEF_FIELD(11, 8, I2S_MODE);
  DEF_FIELD(7, 4, DL2_MODE);
  DEF_FIELD(3, 0, DL1_MODE);
  static auto Get() { return hwreg::RegisterAddr<AFE_DAC_CON1>(0x014); }
};

// AFE I2S Control Register 0.
class AFE_I2S_CON : public hwreg::RegisterBase<AFE_I2S_CON, uint32_t> {
 public:
  DEF_BIT(31, PHASE_SHIFT_FIX);
  DEF_BIT(30, BCK_NEG_EG_LATCH);
  DEF_BIT(29, BCK_INV);
  DEF_BIT(28, I2SIN_PAD_SEL);
  DEF_BIT(20, I2S_LOOPBACK);
  DEF_BIT(12, I2S1_HD_EN);
  DEF_BIT(7, INV_PAD_CTRL);
  DEF_BIT(5, INV_LRCK);
  DEF_BIT(3, I2S_FMT);
  DEF_BIT(2, I2S_SRC);
  DEF_BIT(1, I2S_WLEN);
  DEF_BIT(0, I2S_EN);
  static auto Get() { return hwreg::RegisterAddr<AFE_I2S_CON>(0x018); }
};

// AFE Connection Register 1.
// _R Controls enabling the right shift 1 bit from Ixx to Oyy.
// _S Controls turning on the path from Ixx to Oyy.
class AFE_CONN1 : public hwreg::RegisterBase<AFE_CONN1, uint32_t> {
 public:
  DEF_BIT(31, I08_O03_R);
  DEF_BIT(30, I07_O03_R);
  DEF_BIT(29, I06_O03_R);
  DEF_BIT(28, I04_O03_R);
  DEF_BIT(27, I05_O03_R);
  DEF_BIT(26, I00_O03_R);

  DEF_BIT(24, I08_O03_S);
  DEF_BIT(23, I07_O03_S);
  DEF_BIT(22, I06_O03_S);
  DEF_BIT(21, I05_O03_S);
  DEF_BIT(20, I04_O03_S);
  DEF_BIT(19, I03_O03_S);
  DEF_BIT(18, I02_O03_S);
  DEF_BIT(17, I01_O03_S);
  DEF_BIT(16, I00_O03_S);

  DEF_BIT(15, I08_O02_R);
  DEF_BIT(14, I07_O02_R);
  DEF_BIT(13, I06_O02_R);
  DEF_BIT(12, I04_O02_R);
  DEF_BIT(11, I05_O02_R);
  DEF_BIT(10, I00_O02_R);

  DEF_BIT(8, I08_O02_S);
  DEF_BIT(7, I07_O02_S);
  DEF_BIT(6, I06_O02_S);
  DEF_BIT(5, I05_O02_S);
  DEF_BIT(4, I04_O02_S);
  DEF_BIT(3, I03_O02_S);
  DEF_BIT(2, I02_O02_S);
  DEF_BIT(1, I01_O02_S);
  DEF_BIT(0, I00_O02_S);
  static auto Get() { return hwreg::RegisterAddr<AFE_CONN1>(0x024); }
};

// AFE Connection Register 2.
// _R Controls enabling the right shift 1 bit from Ixx to Oyy.
// _S Controls turning on the path from Ixx to Oyy.
class AFE_CONN2 : public hwreg::RegisterBase<AFE_CONN2, uint32_t> {
 public:
  DEF_BIT(31, I08_O08_R);
  DEF_BIT(30, I06_O08_R);
  DEF_BIT(29, I04_O08_R);

  DEF_BIT(28, I07_O07_R);
  DEF_BIT(27, I05_O07_R);
  DEF_BIT(26, I03_O07_R);

  DEF_BIT(24, I08_O06_S);
  DEF_BIT(23, I06_O06_S);
  DEF_BIT(22, I04_O06_S);
  DEF_BIT(21, I01_O06_S);

  DEF_BIT(20, I07_O05_S);
  DEF_BIT(19, I05_O05_S);
  DEF_BIT(18, I03_O05_S);
  DEF_BIT(17, I02_O05_S);
  DEF_BIT(16, I00_O05_S);

  DEF_BIT(15, I08_O04_R);
  DEF_BIT(14, I07_O04_R);
  DEF_BIT(13, I06_O04_R);
  DEF_BIT(12, I04_O04_R);
  DEF_BIT(11, I05_O04_R);
  DEF_BIT(10, I00_O04_R);

  DEF_BIT(8, I08_O04_S);
  DEF_BIT(7, I07_O04_S);
  DEF_BIT(6, I06_O04_S);
  DEF_BIT(5, I05_O04_S);
  DEF_BIT(4, I04_O04_S);
  DEF_BIT(3, I03_O04_S);
  DEF_BIT(2, I02_O04_S);
  DEF_BIT(1, I01_O04_S);
  DEF_BIT(0, I00_O04_S);
  static auto Get() { return hwreg::RegisterAddr<AFE_CONN2>(0x028); }
};

// AFE Connection Register 3.
// _R Controls enabling the right shift 1 bit from Ixx to Oyy.
// _S Controls turning on the path from Ixx to Oyy.
class AFE_CONN3 : public hwreg::RegisterBase<AFE_CONN3, uint32_t> {
 public:
  DEF_BIT(31, I16_O03_R);
  DEF_BIT(30, I15_O03_R);
  DEF_BIT(29, I16_O03_S);
  DEF_BIT(28, I15_O03_S);

  DEF_BIT(26, I16_O02_R);
  DEF_BIT(25, I15_O02_R);
  DEF_BIT(24, I16_O02_S);
  DEF_BIT(23, I15_O02_S);

  DEF_BIT(21, I16_O01_R);
  DEF_BIT(20, I15_O01_R);
  DEF_BIT(19, I16_O01_S);
  DEF_BIT(18, I15_O01_S);

  DEF_BIT(16, I16_O00_R);
  DEF_BIT(15, I15_O00_R);
  DEF_BIT(14, I16_O00_S);
  DEF_BIT(13, I15_O00_S);

  DEF_BIT(10, I08_O12_S);
  DEF_BIT(9, I06_O12_S);
  DEF_BIT(8, I07_O11_S);
  DEF_BIT(7, I05_O11_S);
  DEF_BIT(6, I02_O11_S);

  DEF_BIT(5, I08_O10_S);
  DEF_BIT(4, I06_O10_S);
  DEF_BIT(3, I04_O10_S);

  DEF_BIT(2, I07_O09_S);
  DEF_BIT(1, I05_O09_S);
  DEF_BIT(0, I03_O09_S);
  static auto Get() { return hwreg::RegisterAddr<AFE_CONN3>(0x02C); }
};

// AFE I2S Control Register 1.
class AFE_I2S_CON1 : public hwreg::RegisterBase<AFE_I2S_CON1, uint32_t> {
 public:
  DEF_BIT(31, I2S2_LR_SWAP);
  DEF_BIT(18, I2S2_TDMOUT_MUX);
  DEF_BIT(12, I2S2_HD_EN);
  DEF_FIELD(11, 8, I2S2_OUT_MODE);
  DEF_BIT(5, INV_LRCK);
  DEF_BIT(3, I2S2_FMT);
  DEF_BIT(1, I2S2_WLEN);
  DEF_BIT(0, I2S2_EN);
  static auto Get() { return hwreg::RegisterAddr<AFE_I2S_CON1>(0x034); }
};

// AFE I2S Control Register 2.
class AFE_I2S_CON2 : public hwreg::RegisterBase<AFE_I2S_CON2, uint32_t> {
 public:
  DEF_BIT(31, I2S3_LR_SWAP);
  DEF_FIELD(28, 24, I2S3_UPDATE_WORD);
  DEF_BIT(23, I2S3_bck_inv);
  DEF_BIT(22, I2S3_fpga_bit_test);
  DEF_BIT(21, I2S3_fpga_bit);
  DEF_BIT(20, I2S3_LOOPBACK);
  DEF_BIT(12, I2S3_HD_EN);
  DEF_FIELD(11, 8, I2S3_OUT_MODE);
  DEF_BIT(3, I2S3_FMT);
  DEF_BIT(1, I2S3_WLEN);
  DEF_BIT(0, I2S3_EN);
  static auto Get() { return hwreg::RegisterAddr<AFE_I2S_CON2>(0x038); }
};

// AFE Merge Interface Control Register.
class AFE_MRGIF_CON : public hwreg::RegisterBase<AFE_MRGIF_CON, uint32_t> {
 public:
  DEF_FIELD(23, 20, MRGIF_I2S_MODE);
  DEF_BIT(19, MRGIF_LOOPBACK);
  DEF_BIT(16, MRGIF_I2S_EN);
  DEF_BIT(14, MRG_CLK_NO_INV);
  DEF_BIT(13, MRG_I2S_TX_DIS);
  DEF_BIT(12, MRG_CNT_CLR);
  DEF_FIELD(11, 8, MRG_SYNC_DLY);
  DEF_FIELD(7, 6, MRG_CLK_EDGE_DLY);
  DEF_FIELD(5, 4, MRG_CLK_DLY);
  DEF_BIT(0, MRGIF_EN);
  static auto Get() { return hwreg::RegisterAddr<AFE_MRGIF_CON>(0x03C); }
};

// AFE DL1 Base Address Register.
class AFE_DL1_BASE : public hwreg::RegisterBase<AFE_DL1_BASE, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AFE_DL1_BASE>(0x040); }
};

// AFE DL1 Base Cursor Register.
class AFE_DL1_CUR : public hwreg::RegisterBase<AFE_DL1_CUR, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AFE_DL1_CUR>(0x044); }
};

// AFE DL1 End Adress Register.
class AFE_DL1_END : public hwreg::RegisterBase<AFE_DL1_END, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AFE_DL1_END>(0x048); }
};

// AFE I2S Control Register 3.
class AFE_I2S_CON3 : public hwreg::RegisterBase<AFE_I2S_CON3, uint32_t> {
 public:
  DEF_BIT(31, I2S4_LR_SWAP);
  DEF_BIT(12, I2S4_HD_EN);
  DEF_FIELD(11, 8, I2S4_OUT_MODE);
  DEF_BIT(5, INV_LRCK);
  DEF_BIT(3, I2S4_FMT);
  DEF_BIT(1, I2S4_WLEN);
  DEF_BIT(0, I2S4_EN);
  static auto Get() { return hwreg::RegisterAddr<AFE_I2S_CON3>(0x04C); }
};

// AFE Connection 24 bit Register.
class AFE_CONN_24BIT : public hwreg::RegisterBase<AFE_CONN_24BIT, uint32_t> {
 public:
  DEF_BIT(22, O22_24BIT);
  DEF_BIT(21, O21_24BIT);
  DEF_BIT(20, O20_24BIT);
  DEF_BIT(19, O19_24BIT);
  DEF_BIT(18, O18_24BIT);
  DEF_BIT(17, O17_24BIT);
  DEF_BIT(16, O16_24BIT);
  DEF_BIT(15, O15_24BIT);
  DEF_BIT(14, O14_24BIT);
  DEF_BIT(13, O13_24BIT);
  DEF_BIT(12, O12_24BIT);
  DEF_BIT(11, O11_24BIT);
  DEF_BIT(10, O10_24BIT);
  DEF_BIT(9, O09_24BIT);
  DEF_BIT(8, O08_24BIT);
  DEF_BIT(7, O07_24BIT);
  DEF_BIT(6, O06_24BIT);
  DEF_BIT(5, O05_24BIT);
  DEF_BIT(4, O04_24BIT);
  DEF_BIT(3, O03_24BIT);
  DEF_BIT(2, O02_24BIT);
  DEF_BIT(1, O01_24BIT);
  DEF_BIT(0, O00_24BIT);
  static auto Get() { return hwreg::RegisterAddr<AFE_CONN_24BIT>(0x06C); }
};

// AFE VUL Base Address Register.
class AFE_VUL_BASE : public hwreg::RegisterBase<AFE_VUL_BASE, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AFE_VUL_BASE>(0x080); }
};

// AFE VUL End Adress Register.
class AFE_VUL_END : public hwreg::RegisterBase<AFE_VUL_END, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AFE_VUL_END>(0x088); }
};

// AFE VUL Base Cursor Register.
class AFE_VUL_CUR : public hwreg::RegisterBase<AFE_VUL_CUR, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AFE_VUL_CUR>(0x08C); }
};

// AFE Uplink SRC Control Register.
class AFE_ADDA_UL_SRC_CON0 : public hwreg::RegisterBase<AFE_ADDA_UL_SRC_CON0, uint32_t> {
 public:
  DEF_BIT(31, c_comb_out_sin_gen_ctl);
  DEF_BIT(30, c_baseband_sin_gen_ctl);
  DEF_FIELD(29, 27, c_digmic_phase_sel_ch1_ctl);
  DEF_FIELD(26, 24, c_digmic_phase_sel_ch2_ctl);
  DEF_BIT(23, c_two_digital_mic_ctl);
  DEF_BIT(22, ul_mode_3p25m_ch2_ctl);
  DEF_BIT(21, ul_mode_3p25m_ch1_ctl);
  DEF_FIELD(20, 19, ul_voice_mode_ch2_ctl);
  DEF_FIELD(18, 17, ul_voice_mode_ch1_ctl);
  DEF_BIT(10, ul_iir_on_tmp_ctl);
  DEF_FIELD(9, 7, ul_iirmode_ctl);
  DEF_BIT(5, digmic_3p25m_1p625m_sel_ctl);
  DEF_BIT(4, agc_260k_sel_ch2_ctl);
  DEF_BIT(3, agc_260k_sel_ch1_ctl);
  DEF_BIT(2, ul_loop_back_mode_ctl);
  DEF_BIT(1, ul_sdm_3_level_ctl);
  DEF_BIT(0, ul_src_on_tmp_ctl);
  static auto Get() { return hwreg::RegisterAddr<AFE_ADDA_UL_SRC_CON0>(0x114); }
};

// AFE DL SRC2 Control Register.
class AFE_ADDA_DL_SRC2_CON0 : public hwreg::RegisterBase<AFE_ADDA_DL_SRC2_CON0, uint32_t> {
 public:
  DEF_BIT(0, dl_2_src_on_tmp_ctl_pre);
  static auto Get() { return hwreg::RegisterAddr<AFE_ADDA_DL_SRC2_CON0>(0x108); }
};

// AFE ADDA Top Control Register.
class AFE_ADDA_TOP_CON0 : public hwreg::RegisterBase<AFE_ADDA_TOP_CON0, uint32_t> {
 public:
  DEF_FIELD(15, 12, c_loop_back_mode_ctl);
  DEF_BIT(0, c_ext_adc_ctl);
  static auto Get() { return hwreg::RegisterAddr<AFE_ADDA_TOP_CON0>(0x108); }
};

// AFE Sine-wave Gen Config 0.  Called AFE_SINEGEN_CON0 in some of the docs.
class AFE_SGEN_CON0 : public hwreg::RegisterBase<AFE_SGEN_CON0, uint32_t> {
 public:
  DEF_FIELD(31, 28, INNER_LOOP_BACK_MODE);
  DEF_BIT(27, IN_OUT_SEL);
  DEF_BIT(26, dac_en);
  DEF_BIT(25, mute_sw_ch2);
  DEF_BIT(24, mute_sw_ch1);
  DEF_FIELD(23, 20, sine_mode_ch2);
  DEF_FIELD(19, 17, amp_div_ch2);
  DEF_FIELD(16, 12, freq_div_ch2);
  DEF_FIELD(11, 8, sine_mode_ch1);
  DEF_FIELD(7, 5, amp_div_ch1);
  DEF_FIELD(4, 0, freq_div_ch1);
  static auto Get() { return hwreg::RegisterAddr<AFE_SGEN_CON0>(0x1F0); }
};

// AFE SGEN for TDM OUT Control.
class AFE_SINEGEN_CON_TDM : public hwreg::RegisterBase<AFE_SINEGEN_CON_TDM, uint32_t> {
 public:
  DEF_BIT(28, sgen_tdm_input_en);
  DEF_BIT(24, c_dac_en_tdm);
  DEF_BIT(20, c_mute_sw_tdm2);
  DEF_FIELD(19, 17, c_amp_div_tdm2);
  DEF_FIELD(16, 12, c_freq_div_tdm2);
  DEF_BIT(8, c_mute_sw_tdm1);
  DEF_FIELD(7, 5, c_amp_div_tdm1);
  DEF_FIELD(4, 0, c_freq_div_tdm1);
  static auto Get() { return hwreg::RegisterAddr<AFE_SINEGEN_CON_TDM>(0x1F8); }
};

// AFE TDMIN Connection Control.
class AFE_CONN_TDMIN_CON : public hwreg::RegisterBase<AFE_CONN_TDMIN_CON, uint32_t> {
 public:
  DEF_FIELD(5, 3, o_41_cfg);
  DEF_FIELD(2, 0, o_40_cfg);
  static auto Get() { return hwreg::RegisterAddr<AFE_CONN_TDMIN_CON>(0x39C); }
};

// AFE TDM Config 1.
class AFE_TDM_CON1 : public hwreg::RegisterBase<AFE_TDM_CON1, uint32_t> {
 public:
  DEF_BIT(3, DELAY_DATA);
  DEF_BIT(0, TDM_EN);
  static auto Get() { return hwreg::RegisterAddr<AFE_TDM_CON1>(0x548); }
};

// AFE TDM Config 2.
class AFE_TDM_CON2 : public hwreg::RegisterBase<AFE_TDM_CON2, uint32_t> {
 public:
  DEF_FIELD(31, 24, TDM_FIX_VALUE);
  DEF_BIT(20, TDM_I2S_LOOPBACK);
  DEF_BIT(16, TDM_FIX_VALUE_SEL);
  static auto Get() { return hwreg::RegisterAddr<AFE_TDM_CON2>(0x54C); }
};

// AFE TDM IN Control Register 0.
class AFE_TDM_IN_CON1 : public hwreg::RegisterBase<AFE_TDM_IN_CON1, uint32_t> {
 public:
  DEF_FIELD(31, 24, LRCK_TDM_WIDTH);
  DEF_FIELD(22, 21, tdm_in_loopback_ch);  // Datasheet says tdm_in_loopack_ch.
  DEF_BIT(20, tdm_in_loopback);
  DEF_FIELD(19, 16, disable_out);
  DEF_FIELD(13, 12, fast_lrck_cycle_sel);
  DEF_FIELD(11, 10, tdm_channel);
  DEF_FIELD(9, 8, tdm_wlen);
  DEF_BIT(4, tdm_lr_swap);
  DEF_BIT(3, tdm_fmt);
  DEF_BIT(2, tdm_lrck_inv);
  DEF_BIT(1, tdm_bck_inv);
  DEF_BIT(0, tdm_en);
  static auto Get() { return hwreg::RegisterAddr<AFE_TDM_IN_CON1>(0x588); }
};

// AFE IRQ10 MCU Counter Register.
class AFE_IRQ_MCU_CNT10 : public hwreg::RegisterBase<AFE_IRQ_MCU_CNT10, uint32_t> {
 public:
  DEF_FIELD(17, 0, cnt);
  static auto Get() { return hwreg::RegisterAddr<AFE_IRQ_MCU_CNT10>(0x8DC); }
};

// AFE HDMI_IN_2CH Config Register.
class AFE_HDMI_IN_2CH_CON0 : public hwreg::RegisterBase<AFE_HDMI_IN_2CH_CON0, uint32_t> {
 public:
  DEF_BIT(2, AFE_HDMI_IN_2CH_R_MONO);
  DEF_BIT(1, AFE_HDMI_IN_2CH_DATA);
  DEF_BIT(0, AFE_HDMI_IN_2CH_OUT_ON);
  static auto Get() { return hwreg::RegisterAddr<AFE_HDMI_IN_2CH_CON0>(0x9C0); }
};

// AFE HDMI_IN_2CH Base Address Register.
class AFE_HDMI_IN_2CH_BASE : public hwreg::RegisterBase<AFE_HDMI_IN_2CH_BASE, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AFE_HDMI_IN_2CH_BASE>(0x9C4); }
};

// AFE HDMI_IN_2CH End Adress Register.
class AFE_HDMI_IN_2CH_END : public hwreg::RegisterBase<AFE_HDMI_IN_2CH_END, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AFE_HDMI_IN_2CH_END>(0x9C8); }
};

// AFE HDMI_IN_2CH Base Cursor Register.
class AFE_HDMI_IN_2CH_CUR : public hwreg::RegisterBase<AFE_HDMI_IN_2CH_CUR, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AFE_HDMI_IN_2CH_CUR>(0x9CC); }
};

#endif  // SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_AUDIO_REGS_H_
