// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hwreg/bitfields.h>
#include <zircon/types.h>

//
// Register definitions taken from:
//
//   ALC5663 (ALC5663-CG)
//   32bits Hi-Fi Digital Audio Headphone Amplifier
//   Revision 0.6
//   26 January 2016
//   Realtek Semiconductor Corp.
//

namespace audio::alc5663 {

// Any write to this register will trigger a reset of the codec.
struct ResetAndDeviceIdReg {
  uint16_t data;

  DEF_SUBBIT(data, 1, device_id);  // Device ID: Reading 0 indicates ALC5663.

  static constexpr uint8_t kAddress = 0x0;
};

// Sidetone (repeating mic signal into speaker output) control and configuration.
struct SidetoneControlReg {
  uint16_t data;

  DEF_SUBFIELD(data, 15, 13, sidetone_hpf_fc_s);  // Highpass filter cutoff (R/W)
  DEF_SUBBIT(data, 12, sidetone_hpf_en);          // Enable sidetone highpass filter (R/W)
  DEF_SUBBIT(data, 6, en_sidetone);               // Enable sidetone (R/W)
  DEF_SUBBIT(data, 5, sidetone_boost_sel);        // Sidetone gain (R/W)
  DEF_SUBFIELD(data, 4, 0, sidetone_vol_sel);     // Sidetone volume (R/W)

  static constexpr uint8_t kAddress = 0x18;
};

struct PowerManagementControl1Reg {
  uint16_t data;

  DEF_SUBBIT(data, 15, en_i2s1);        // I2S 1 digital interface power (R/W)
  DEF_SUBBIT(data, 11, pow_dac_l_1);    // Analog DAC L1 power (R/W)
  DEF_SUBBIT(data, 10, pow_dac_r_1);    // Analog DAC R1 power (R/W)
  DEF_SUBBIT(data, 8, pow_ldo_adcref);  // ADC REF LDO power (R/W)
  DEF_SUBBIT(data, 5, fast_ldo_adcref);
  DEF_SUBBIT(data, 4, pow_adc_l);  // Analog ADC power (R/W)

  static constexpr uint8_t kAddress = 0x61;
};

struct PowerManagementControl2Reg {
  uint16_t data;

  DEF_SUBBIT(data, 15, pow_adc_filter);          // ADC digital filter power (R/W)
  DEF_SUBBIT(data, 10, pow_dac_stereo1_filter);  // DAC stereo 1 filter power (R/W)

  static constexpr uint8_t kAddress = 0x62;
};

struct PowerManagementControl3Reg {
  uint16_t data;

  DEF_SUBBIT(data, 15, pow_vref1);  // VREF1 power (R/W)
  DEF_SUBBIT(data, 14, en_fastb1);
  DEF_SUBBIT(data, 13, pow_vref2);  // VREF2 power (R/W)
  DEF_SUBBIT(data, 12, en_fastb2);
  DEF_SUBBIT(data, 9, pow_main_bias);  // MBIAS power (R/W)
  DEF_SUBBIT(data, 7, pow_bg_bias);    // MBIAS bandgap power (R/W)
  DEF_SUBBIT(data, 5, en_l_hp);        // Left headphone amp power (R/W)
  DEF_SUBBIT(data, 4, en_r_hp);        // Right headphone amp power (R/W)
  DEF_SUBFIELD(data, 3, 2, en_amp_hp);
  DEF_SUBFIELD(data, 1, 0, ldo1_dvo);

  static constexpr uint8_t kAddress = 0x63;
};

struct PowerManagementControl4Reg {
  uint16_t data;

  DEF_SUBBIT(data, 15, pow_bst1);      // MIC BST1 power (R/W)
  DEF_SUBBIT(data, 11, pow_micbias1);  // MICBIAS1 power (R/W)
  DEF_SUBBIT(data, 10, pow_micbias2);  // MICBIAS2 power (R/W)
  DEF_SUBBIT(data, 1, pow_recmix1);    // RECMIX power (R/W)

  static constexpr uint8_t kAddress = 0x64;
};

struct PowerManagementControl5Reg {
  uint16_t data;

  DEF_SUBBIT(data, 6, pow_pll);  // PLL power (R/W)

  static constexpr uint8_t kAddress = 0x65;
};

struct I2s1DigitalInterfaceControlReg {
  uint16_t data;

  DEF_SUBBIT(data, 15, sel_i2s1_ms);
  DEF_SUBBIT(data, 14, config_i2s1_);
  DEF_SUBFIELD(data, 13, 12, en_i2s1_out_comp);
  DEF_SUBFIELD(data, 11, 10, en_i2s1_in_comp);
  DEF_SUBBIT(data, 8, inv_i2s1_bclk);
  DEF_SUBBIT(data, 6, en_i2s1_mono);
  DEF_SUBFIELD(data, 5, 4, sel_i2s1_len);
  DEF_SUBFIELD(data, 2, 0, sel_i2s1_format);

  static constexpr uint8_t kAddress = 0x70;
};

}  // namespace audio::alc5663
