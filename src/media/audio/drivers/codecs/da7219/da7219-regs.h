// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_REGS_H_
#define SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_REGS_H_

#include <hwreg/i2c.h>

namespace audio::da7219 {

// This class adds defaults and helpers to the hwreg-i2c library.
// Since all registers read/write one byte at the time IntType is uint8_t and AddrIntSize 1.
template <typename DerivedType, uint8_t address>
struct I2cRegister : public hwreg::I2cRegisterBase<DerivedType, uint8_t, 1> {
  // Read from I2C and log errors.
  static zx::result<DerivedType> Read(fidl::ClientEnd<fuchsia_hardware_i2c::Device>& i2c) {
    auto ret = Get();
// #define TRACE_I2C
#ifdef TRACE_I2C
    printf("Reading register 0x%02X\n", ret.reg_addr());
#endif
    zx_status_t status = ret.ReadFrom(i2c);
    if (status != ZX_OK) {
      // TODO(102079): Add logging here compatible with DFv2's logger_ requirement.
      return zx::error(status);
    }
    return zx::ok(ret);
  }
  // Write to I2C and log errors.
  zx_status_t Write(fidl::ClientEnd<fuchsia_hardware_i2c::Device>& i2c) {
#ifdef TRACE_I2C
    printf("Writting register 0x%02X\n",
           hwreg::I2cRegisterBase<DerivedType, uint8_t, 1>::reg_addr());
#endif
    // TODO(102079): Add logging here compatible with DFv2's logger_ requirement.
    return hwreg::I2cRegisterBase<DerivedType, uint8_t, 1>::WriteTo(i2c);
  }
  static DerivedType Get() { return hwreg::I2cRegisterAddr<DerivedType>(address).FromValue(0); }
};

// SR. Sample rate control.
struct Sr : public I2cRegister<Sr, 0x17> {
  static constexpr uint8_t k8000Hz = 1;  // Value 0 not in datasheet.
  static constexpr uint8_t k11025Hz = 2;
  static constexpr uint8_t k12000Hz = 3;
  static constexpr uint8_t k16000Hz = 5;  // Value 4 not in datasheet.
  static constexpr uint8_t k22050Hz = 6;
  static constexpr uint8_t k24000Hz = 7;
  static constexpr uint8_t k32000Hz = 9;  // Value 8 not in datasheet.
  static constexpr uint8_t k44100Hz = 10;
  static constexpr uint8_t k48000Hz = 11;
  static constexpr uint8_t k88200Hz = 14;  // Values 12 and 13 not in datasheet.
  static constexpr uint8_t k96000Hz = 15;
  DEF_FIELD(3, 0, sr);
};

// PLL_CTRL.
struct PllCtrl : public I2cRegister<PllCtrl, 0x20> {
  DEF_FIELD(7, 6, pll_mode);
  static constexpr uint8_t kPllModeBypassMode = 0;
  static constexpr uint8_t kPllModeNormalMode = 1;
  static constexpr uint8_t kPllModeSrm = 2;
  DEF_BIT(5, pll_mclk_sqr_en);
  DEF_FIELD(4, 2, pll_indiv);
  static constexpr uint8_t kPllIndiv2to4p5MHz = 0;
  static constexpr uint8_t kPllIndiv4p5to9MHz = 1;
  static constexpr uint8_t kPllIndiv9to18MHz = 2;
  static constexpr uint8_t kPllIndiv18to36MHz = 3;
  static constexpr uint8_t kPllIndiv36plusMHz = 4;
};

// DIG_ROUTING_DAI.
struct DigRoutingDai : public I2cRegister<DigRoutingDai, 0x2a> {
  DEF_FIELD(5, 4, dai_r_src);
  DEF_FIELD(1, 0, dai_l_src);
  static constexpr uint8_t kAdcLeft = 0;
  static constexpr uint8_t kToneGenerator = 1;
  static constexpr uint8_t DaiInputLeftDataDaiMonoMix = 2;
  static constexpr uint8_t DaiInputRightDataDaiMonoMix = 3;
};

// DAI_CTRL.
struct DaiCtrl : public I2cRegister<DaiCtrl, 0x2c> {
  DEF_BIT(7, dai_en);
  DEF_FIELD(5, 4, dai_ch_num);
  static constexpr uint8_t kDaiChNumNoChannelsAreEnabled = 0;
  static constexpr uint8_t kDaiChNumLeftChannelIsEnabled = 1;
  static constexpr uint8_t kDaiChNumLeftAndRightChannelsAreEnabled = 2;
  DEF_FIELD(3, 2, dai_word_length);
  static constexpr uint8_t kDaiWordLength16BitsPerChannel = 0;
  static constexpr uint8_t kDaiWordLength20BitsPerChannel = 1;
  static constexpr uint8_t kDaiWordLength24BitsPerChannel = 2;
  static constexpr uint8_t kDaiWordLength32BitsPerChannel = 3;
  DEF_FIELD(1, 0, dai_format);
  static constexpr uint8_t kDaiFormatI2sMode = 0;
  static constexpr uint8_t kDaiFormatLeftJustifiedMode = 1;
  static constexpr uint8_t kDaiFormatRightJustifiedMode = 2;
  static constexpr uint8_t kDaiFormatDspMode = 3;
};

// DAI_TDM_CTRL.
struct DaiTdmCtrl : public I2cRegister<DaiTdmCtrl, 0x2d> {
  DEF_BIT(7, dai_tdm_mode_en);
  DEF_BIT(6, dai_oe);
  DEF_FIELD(1, 0, dai_tdm_ch_en);
  static constexpr uint8_t kLeftChannelAndRightChannelBothDisabled = 0;
  static constexpr uint8_t kLeftChannelEnabledRightChannelDisabled = 1;
  static constexpr uint8_t kLeftChannelDisabledRightChannelEnabled = 2;
  static constexpr uint8_t kLeftChannelAndRightChannelBothEnabled = 3;
};

// MIXIN_L_SELECT
struct MixinLSelect : public I2cRegister<MixinLSelect, 0x33> {
  DEF_BIT(0, mixin_l_mix_select);
};

// MIC_1_GAIN
struct Mic1Gain : public I2cRegister<Mic1Gain, 0x39> {
  DEF_FIELD(2, 0, mic_1_amp_gain);
  static constexpr uint8_t kMinus6dB = 0;
  static constexpr uint8_t k0dB = 1;
  static constexpr uint8_t k6dB = 2;
  static constexpr uint8_t k12dB = 3;
  static constexpr uint8_t k18dB = 4;
  static constexpr uint8_t k24dB = 5;
  static constexpr uint8_t k30dB = 6;
  static constexpr uint8_t k36dB = 7;
};

// CP_CTRL.
struct CpCtrl : public I2cRegister<CpCtrl, 0x47> {
  DEF_BIT(7, cp_en);
  DEF_FIELD(5, 4, cp_mchange);
  static constexpr uint8_t kCpMchangeLargestOutputVolumeLevel = 1;
  static constexpr uint8_t kCpMchangeDacVol = 2;
  static constexpr uint8_t kCpMchangeSignalMagnitude = 3;
};

// MIXOUT_L_SELECT.
struct MixoutLSelect : public I2cRegister<MixoutLSelect, 0x4b> {
  DEF_BIT(0, mixout_l_mix_select);
};

// MIXOUT_R_SELECT.
struct MixoutRSelect : public I2cRegister<MixoutRSelect, 0x4c> {
  DEF_BIT(0, mixout_r_mix_select);
};

// MICBIAS_CTRL.
struct MicbiasCtrl : public I2cRegister<MicbiasCtrl, 0x62> {
  DEF_BIT(3, micbias1_en);
  DEF_FIELD(2, 0, micbias1_level);
};

// MIC_1_CTRL.
struct Mic1Ctrl : public I2cRegister<Mic1Ctrl, 0x63> {
  DEF_BIT(7, mic_1_amp_en);
  DEF_BIT(6, mic_1_amp_mute_en);
  DEF_BIT(5, mic_1_amp_ramp_en);
};

// MIXIN_L_CTRL.
struct MixinLCtrl : public I2cRegister<MixinLCtrl, 0x65> {
  DEF_BIT(7, mixin_l_amp_en);
  DEF_BIT(6, mixin_l_amp_mute_en);
  DEF_BIT(5, mixin_l_amp_ramp_en);
  DEF_BIT(4, mixin_l_amp_zc_en);
  DEF_BIT(3, mixin_l_mix_en);
};

// ADC_L_CTRL.
struct AdcLCtrl : public I2cRegister<AdcLCtrl, 0x67> {
  DEF_BIT(7, adc_l_en);
  DEF_BIT(6, adc_l_mute_en);
  DEF_BIT(5, adc_l_ramp_en);
};

// DAC_L_CTRL.
struct DacLCtrl : public I2cRegister<DacLCtrl, 0x69> {
  DEF_BIT(7, dac_l_en);
  DEF_BIT(6, dac_l_mute_en);
  DEF_BIT(5, dac_l_ramp_en);
};

// DAC_R_CTRL.
struct DacRCtrl : public I2cRegister<DacRCtrl, 0x6a> {
  DEF_BIT(7, dac_r_en);
  DEF_BIT(6, dac_r_mute_en);
  DEF_BIT(5, dac_r_ramp_en);
};

// HP_L_CTRL.
struct HpLCtrl : public I2cRegister<HpLCtrl, 0x6b> {
  DEF_BIT(7, hp_l_amp_en);
  DEF_BIT(6, hp_l_amp_mute_en);
  DEF_BIT(5, hp_l_amp_ramp_en);
  DEF_BIT(4, hp_l_amp_zc_en);
  DEF_BIT(3, hp_l_amp_oe);
  DEF_BIT(2, hp_l_amp_min_gain_en);
};

// HP_R_CTRL.
struct HpRCtrl : public I2cRegister<HpRCtrl, 0x6c> {
  DEF_BIT(7, hp_r_amp_en);
  DEF_BIT(6, hp_r_amp_mute_en);
  DEF_BIT(5, hp_r_amp_ramp_en);
  DEF_BIT(4, hp_r_amp_zc_en);
  DEF_BIT(3, hp_r_amp_oe);
  DEF_BIT(2, hp_r_amp_min_gain_en);
};

// MIXOUT_L_CTRL.
struct MixoutLCtrl : public I2cRegister<MixoutLCtrl, 0x6e> {
  DEF_BIT(7, mixout_l_amp_en);
};

// MIXOUT_R_CTRL.
struct MixoutRCtrl : public I2cRegister<MixoutRCtrl, 0x6f> {
  DEF_BIT(7, mixout_r_amp_en);
};

// CHIP_ID1.
struct ChipId1 : public I2cRegister<ChipId1, 0x81> {
  DEF_FIELD(7, 0, chip_id1);
};

// CHIP_ID2.
struct ChipId2 : public I2cRegister<ChipId2, 0x82> {
  DEF_FIELD(7, 0, chip_id2);
};

// CHIP_REVISION.
struct ChipRevision : public I2cRegister<ChipRevision, 0x83> {
  DEF_FIELD(7, 4, chip_major);
  DEF_FIELD(3, 0, chip_minor);
};

// ACCDET_STATUS_A.
struct AccdetStatusA : public I2cRegister<AccdetStatusA, 0xc0> {
  DEF_BIT(3, micbias_up_sts);
  DEF_BIT(2, jack_pin_order_sts);
  DEF_BIT(1, jack_type_sts);
  DEF_BIT(0, jack_insertion_sts);
};

// ACCDET_STATUS_B.
struct AccdetStatusB : public I2cRegister<AccdetStatusB, 0xc1> {
  DEF_FIELD(7, 0, button_type_sts);
};

// ACCDET_IRQ_EVENT_A.
struct AccdetIrqEventA : public I2cRegister<AccdetIrqEventA, 0xc2> {
  DEF_BIT(2, e_jack_detect_complete);
  DEF_BIT(1, e_jack_removed);
  DEF_BIT(0, e_jack_inserted);
};

// ACCDET_IRQ_EVENT_B.
struct AccdetIrqEventB : public I2cRegister<AccdetIrqEventB, 0xc3> {
  DEF_BIT(7, e_button_a_released);
  DEF_BIT(6, e_button_b_released);
  DEF_BIT(5, e_button_c_released);
  DEF_BIT(4, e_button_d_released);
  DEF_BIT(3, e_button_d_pressed);
  DEF_BIT(2, e_button_c_pressed);
  DEF_BIT(1, e_button_b_pressed);
  DEF_BIT(0, e_button_a_pressed);
};

// ACCDET_IRQ_MASK_A
struct AccdetIrqMaskA : public I2cRegister<AccdetIrqMaskA, 0xc4> {
  DEF_BIT(2, m_jack_detect_comp);
  DEF_BIT(1, m_jack_removed);
  DEF_BIT(0, m_jack_inserted);
};

// ACCDET_IRQ_MASK_B.
struct AccdetIrqMaskB : public I2cRegister<AccdetIrqMaskB, 0xc5> {
  DEF_BIT(7, m_button_a_release);
  DEF_BIT(6, m_button_b_release);
  DEF_BIT(5, m_button_c_release);
  DEF_BIT(4, m_button_d_release);
  DEF_BIT(3, m_button_d_pressed);
  DEF_BIT(2, m_button_c_pressed);
  DEF_BIT(1, m_button_b_pressed);
  DEF_BIT(0, m_button_a_pressed);
};

// ACCDET_CONFIG_1.
struct AccdetConfig1 : public I2cRegister<AccdetConfig1, 0xc6> {
  DEF_BIT(7, pin_order_det_en);
  DEF_BIT(6, jack_type_det_en);
  DEF_FIELD(5, 4, mic_det_thresh);
  static constexpr uint8_t kMicDetThresh200Ohms = 0;
  static constexpr uint8_t kMicDetThresh500Ohms = 1;
  static constexpr uint8_t kMicDetThresh750Ohms = 2;
  static constexpr uint8_t kMicDetThresh1000Ohms = 3;
  DEF_FIELD(3, 1, button_config);
  static constexpr uint8_t kButtonConfigDisabled = 0;
  static constexpr uint8_t kButtonConfig2ms = 1;
  static constexpr uint8_t kButtonConfig5ms = 2;
  static constexpr uint8_t kButtonConfig10ms = 3;
  static constexpr uint8_t kButtonConfig50ms = 4;
  static constexpr uint8_t kButtonConfig100ms = 5;
  static constexpr uint8_t kButtonConfig200ms = 6;
  static constexpr uint8_t kButtonConfig500ms = 7;
  DEF_BIT(0, accdet_en);
};

// SYSTEM_ACTIVE.
struct SystemActive : public I2cRegister<SystemActive, 0xfd> {
  DEF_BIT(0, system_active);
};

}  // namespace audio::da7219

#endif  // SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_REGS_H_
