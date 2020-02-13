// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_ALC5663_ALC5663_REGISTERS_H_
#define SRC_MEDIA_AUDIO_DRIVERS_ALC5663_ALC5663_REGISTERS_H_

#include <zircon/types.h>

#include <hwreg/bitfields.h>

//
// Register definitions taken from:
//
//   ALC5663 (ALC5663-CG)
//   32bits Hi-Fi Digital Audio Headphone Amplifier
//   Revision 0.9
//   6 April 2017
//   Realtek Semiconductor Corp.
//
// Some register definitions are marked with "(??)". These are definitions that are
// not referenced in the datasheet above, but are nevertheless required for audio
// output to work. The values were derived by comparing the I2C register values of
// an ALC5663 codec playing audio on a working system against the values specified
// by the ALC5663 datasheets. The names and field locations of such registers are
// guesses based on inspecting datasheets of other Realtek codecs, and empirically
// trying different values. Fields marked with "(??)" should not be trusted.

namespace audio::alc5663 {

// Register values used by clock dividers in the ALC5663.
enum class ClockDivisionRate {
  DivideBy1 = 0,  // Disable divider.
  DivideBy2 = 1,
  DivideBy3 = 2,
  DivideBy4 = 3,
  DivideBy6 = 4,
  DivideBy8 = 5,
  DivideBy12 = 6,
  DivideBy16 = 7,
};

// Any write to this register will trigger a reset of the codec.
struct ResetAndDeviceIdReg {
  uint16_t data;

  DEF_SUBBIT(data, 1, device_id);  // Device ID: Reading 0 indicates ALC5663.

  static constexpr uint16_t kAddress = 0x0;
};

// Sidetone (repeating mic signal into speaker output) control and configuration.
struct SidetoneControlReg {
  uint16_t data;

  DEF_SUBFIELD(data, 15, 13, sidetone_hpf_fc_s);  // Highpass filter cutoff (R/W)
  DEF_SUBBIT(data, 12, sidetone_hpf_en);          // Enable sidetone highpass filter (R/W)
  DEF_SUBBIT(data, 6, en_sidetone);               // Enable sidetone (R/W)
  DEF_SUBBIT(data, 5, sidetone_boost_sel);        // Sidetone gain (R/W)
  DEF_SUBFIELD(data, 4, 0, sidetone_vol_sel);     // Sidetone volume (R/W)

  static constexpr uint16_t kAddress = 0x18;
};

// Stereo DAC digital volume.
//
// Digital volume can be set from 0 (-65.625dB) to 0xaf (0dB) with
// 0.375dB per step.
struct StereoDacDigitalVolumeReg {
  uint16_t data;

  DEF_SUBFIELD(data, 15, 8, vol_dac1_l);  // Left channel digital volume
  DEF_SUBFIELD(data, 7, 0, vol_dac1_r);   // Right channel digital volume

  static constexpr uint8_t kMinVolume = 0x00;

  // Volume we set the hardware to. (-6.0dB).
  static constexpr uint8_t kTargetVolume = 0x9f;

  // Max volume according to the ALC5663 datasheet.
  //
  // While we should be able to run with no digital volume change, in practice, we have observed
  // distortion of the signal with values this high. We use kTargetVolume instead.
  static constexpr uint8_t kHardwareMaxVolume = 0xaf;

  static constexpr uint16_t kAddress = 0x19;
};

// Stereo DAC Digital Mixer Control.
struct StereoDacDigitalMixerControl {
  uint16_t data;

  DEF_SUBBIT(data, 15, mute_dacl1_mixl);         // Mute DACL to DAC Mixer L
  DEF_SUBBIT(data, 14, gain_dacl1_to_stereo_l);  // Apply -6dB gain from DAC L to DAC Mixer L
  DEF_SUBBIT(data, 13, mute_dacr1_mixl);         // Mute DACR to DAC Mixer L
  DEF_SUBBIT(data, 12, gain_dacr1_to_stereo_l);  // Apply -6dB gain from DAC R to DAC Mixer L
  DEF_SUBBIT(data, 7, mute_dacl1_mixr);          // Mute DACL to DAC Mixer R
  DEF_SUBBIT(data, 6, gain_dacl1_to_stereo_r);   // Apply -6dB gain from DAC L to DAC Mixer R
  DEF_SUBBIT(data, 5, mute_dacr1_mixr);          // Mute DACR to DAC Mixer R
  DEF_SUBBIT(data, 4, gain_dacr1_to_stereo_r);   // Apply -6dB gain from DAC R to DAC Mixer R

  static constexpr uint16_t kAddress = 0x2a;
};

struct BypassStereoDacMixerControlReg {
  uint16_t data;

  DEF_SUBBIT(data, 3, dacl1_source);  // Select DACL Source. (0 == bypass mixers, 1 == use mixers)
  DEF_SUBBIT(data, 2, dacr1_source);  // Select DACR Source. (0 == bypass mixers, 1 == use mixers)

  static constexpr uint16_t kAddress = 0x2d;
};

struct PowerManagementControl1Reg {
  uint16_t data;

  DEF_SUBBIT(data, 15, en_i2s1);        // I2S 1 digital interface power (R/W)
  DEF_SUBBIT(data, 11, pow_dac_l_1);    // Analog DAC L1 power (R/W)
  DEF_SUBBIT(data, 10, pow_dac_r_1);    // Analog DAC R1 power (R/W)
  DEF_SUBBIT(data, 8, pow_ldo_adcref);  // ADC REF LDO power (R/W)
  DEF_SUBBIT(data, 5, fast_ldo_adcref);
  DEF_SUBBIT(data, 4, pow_adc_l);  // Analog ADC power (R/W)

  static constexpr uint16_t kAddress = 0x61;
};

struct PowerManagementControl2Reg {
  uint16_t data;

  DEF_SUBBIT(data, 15, pow_adc_filter);          // ADC digital filter power (R/W)
  DEF_SUBBIT(data, 10, pow_dac_stereo1_filter);  // DAC stereo 1 filter power (R/W)

  static constexpr uint16_t kAddress = 0x62;
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

  static constexpr uint16_t kAddress = 0x63;
};

struct PowerManagementControl4Reg {
  uint16_t data;

  DEF_SUBBIT(data, 15, pow_bst1);      // MIC BST1 power (R/W)
  DEF_SUBBIT(data, 11, pow_micbias1);  // MICBIAS1 power (R/W)
  DEF_SUBBIT(data, 10, pow_micbias2);  // MICBIAS2 power (R/W)
  DEF_SUBBIT(data, 1, pow_recmix1);    // RECMIX power (R/W)

  static constexpr uint16_t kAddress = 0x64;
};

struct PowerManagementControl5Reg {
  uint16_t data;

  DEF_SUBBIT(data, 6, pow_pll);  // PLL power (R/W)

  static constexpr uint16_t kAddress = 0x65;
};

struct PowerManagementControlMisc {
  uint16_t data;

  static constexpr uint16_t kEnable = 0xef;
  DEF_SUBFIELD(data, 15, 8, gating);  // Clock gating (??)

  static constexpr uint16_t kAddress = 0x6e;
};

struct I2s1DigitalInterfaceControlReg {
  uint16_t data;

  // If (1), we read BCLK from the bus ("slave mode" in manual).
  // If (0), we write BCKL to the bus ("master mode" in manual).
  DEF_SUBBIT(data, 15, i2s1_externally_clocked);

  // Configure the I2S1 ADCDAT pin as an output pin (0) or input pin (1).
  DEF_SUBBIT(data, 14, i2s1_adcdac);

  // I2S1 input/output data compression.
  DEF_SUBFIELD(data, 13, 12, i2s1_out_comp);
  DEF_SUBFIELD(data, 11, 10, i2s1_in_comp);

  DEF_SUBBIT(data, 8, inverted_i2s1_bclk);  // I2S1 BCLK polarity. Normal (0) or inverted (1).
  DEF_SUBBIT(data, 6, i2s1_mono);

  // I2S1 Data Length
  enum class DataLength {
    Bits16 = 0,
    Bits20 = 1,
    Bits24 = 2,
    Bits8 = 3,
  };
  DEF_ENUM_SUBFIELD(data, DataLength, 5, 4, i2s1_data_length);

  // I2S1 Data Format
  enum class DataFormat {
    I2sFormat = 0,
    LeftJustified = 1,
  };
  DEF_ENUM_SUBFIELD(data, DataFormat, 2, 0, i2s1_data_format);

  static constexpr uint16_t kAddress = 0x70;
};

struct AdcDacClockControlReg {
  uint16_t data;

  // I2S Clock Pre-Divider (from clk_sys_pre to clk_sys_i2s).
  DEF_ENUM_SUBFIELD(data, ClockDivisionRate, 14, 12, i2s_pre_div);

  // Clock configuration for I2S master mode.
  DEF_ENUM_SUBFIELD(data, ClockDivisionRate, 10, 8, master_i2s_div);
  DEF_SUBFIELD(data, 5, 4, master_clk_source);

  DEF_SUBFIELD(data, 3, 2, dac_oversample_rate);  // Stereo DAC oversample rate
  DEF_SUBFIELD(data, 1, 0, adc_oversample_rate);  // Mono ADC oversample rate

  static constexpr uint16_t kAddress = 0x73;
};

struct GlobalClockControlReg {
  uint16_t data;

  // System clock source.
  enum class SysClk1Source : uint8_t {
    MCLK = 0,
    PLL = 1,
    InternalClock = 2,
  };
  DEF_ENUM_SUBFIELD(data, SysClk1Source, 15, 14, sysclk1_source);

  // PLL source.
  enum class PllSource : uint8_t {
    MCLK = 0,
    BCLK = 1,
    InternalClock = 4,
  };
  DEF_ENUM_SUBFIELD(data, PllSource, 13, 11, pll_source);

  // PLL pre-divider.
  // 0: divide by 1 (i.e., disabled).
  // 1: divide by 2.
  DEF_SUBBIT(data, 3, pll_pre_div);

  // System clock divider for Stereo DAC and Mono ADC filters.
  DEF_ENUM_SUBFIELD(data, ClockDivisionRate, 2, 0, filter_clock_divider);

  static constexpr uint16_t kAddress = 0x80;
};

// Phase-locked loop registers.
//
// The PLL takes an input F_in (from MCLK, BLCK, or Internal Clock; determined by
// GlobalClockControlReg::pll_div) and outputs a clock with frequency F_out:
//
//   F_out = (F_in * (N + 2)) / ((M + 2) * (K + 2))
//
// The ALC5663 manual states outputs should be in the range 2.048MHz to 40MHz,
// and that K is typically 2.
struct PllControl1Reg {
  uint16_t data;

  DEF_SUBFIELD(data, 15, 7, n_code);  // Value for "N".
  DEF_SUBFIELD(data, 4, 0, k_code);   // Value for "K".

  static constexpr uint16_t kAddress = 0x81;
};
struct PllControl2Reg {
  uint16_t data;

  DEF_SUBFIELD(data, 15, 12, m_code);  // Value for "M".

  DEF_SUBBIT(data, 11, bypass_m);  // Ignore the (M + 2) factor.
  DEF_SUBBIT(data, 10, bypass_k);  // Ignore the (K + 2) factor.

  static constexpr uint16_t kAddress = 0x82;
};

// Control registers for ALC5663's asynchronous sampling rate converter
// (ASRC), allowing a system clock that is independent of the I2S BCLK.
struct AsrcControl1Reg {
  uint16_t data;

  DEF_SUBBIT(data, 11, i2s1_asrc);  // Enable global ASRC
  DEF_SUBBIT(data, 10, dac_asrc);   // Enable ASRC for D->A path.
  DEF_SUBBIT(data, 3, adc_asrc);    // Enable ASRC for A->D path.

  static constexpr uint16_t kAddress = 0x83;
};
struct AsrcControl2Reg {
  uint16_t data;

  enum class FilterSource : uint8_t {
    ClkSys = 0,  // Use clk_sys_i2s (after it has been divided by MX-0080[2:0].)
    ASRC = 1,    // Use the clock from the ASRC block.
  };

  // Clock source for the D->A filter.
  DEF_ENUM_SUBFIELD(data, FilterSource, 14, 12, clk_da_filter_source);

  // Clock source for the A->D filter.
  DEF_ENUM_SUBFIELD(data, FilterSource, 2, 0, clk_ad_filter_source);

  static constexpr uint16_t kAddress = 0x84;
};
struct AsrcControl4Reg {
  uint16_t data;

  static constexpr uint32_t kSampleRate48000 = 0x2;
  DEF_SUBFIELD(data, 5, 4, asrc_i2s1_mode);  // ASRC clock source / I2S rate (??)

  static constexpr uint16_t kAddress = 0x86;
};

// Output amplifier.
struct HpAmpControl1Reg {
  uint16_t data;

  DEF_SUBBIT(data, 13, enable_l_hp);  // Enable left channel HP output (??)
  DEF_SUBBIT(data, 12, enable_r_hp);  // Enable right channel HP output (??)

  DEF_SUBBIT(data, 5, pow_pump_l_hp);  // Left charge pump power
  DEF_SUBBIT(data, 4, pow_pump_r_hp);  // Right charge pump power
  DEF_SUBBIT(data, 1, pow_capless_l);  // Left capless block power
  DEF_SUBBIT(data, 0, pow_capless_r);  // Right capless block power

  static constexpr uint16_t kAddress = 0x8e;
};
struct HpAmpControl2Reg {
  uint16_t data;

  DEF_SUBBIT(data, 11, output_l_hp);  // Enable capless HP L output
  DEF_SUBBIT(data, 10, output_r_hp);  // Enable capless HP R output

  // Overcurrent protection (OCP).
  DEF_SUBBIT(data, 2, overcurrent_protection_hp);  // Enable HP OCP
  DEF_SUBFIELD(data, 1, 0, overcurrent_limit_hp);  // HP OCP threshold

  static constexpr uint16_t kAddress = 0x91;
};
struct HpAmpControl3Reg {
  uint16_t data;

  DEF_SUBBIT(data, 9, pow_reg_l_hp);  // HP AMP L VEE regulator power
  DEF_SUBBIT(data, 8, pow_reg_r_hp);  // HP AMP R VEE regulator power

  static constexpr uint16_t kAddress = 0x92;
};

struct InternalClockControlReg {
  uint16_t data;

  DEF_SUBBIT(data, 9, pow_clock_25mhz);  // Enable 25MHz internal clock.
  DEF_SUBBIT(data, 8, pow_clock_1mhz);   // Enable 1MHz internal clock.

  static constexpr uint16_t kAddress = 0x94;
};

struct GeneralControlReg {
  uint16_t data;

  DEF_SUBBIT(data, 0, digital_gate_ctrl);  // MCLK gating.

  static constexpr uint16_t kAddress = 0xfa;
};

struct VersionIdReg {
  uint16_t data;

  DEF_SUBFIELD(data, 15, 0, version_id);

  static constexpr uint16_t kAddress = 0xfd;
};

struct VendorIdReg {
  uint16_t data;

  static const uint16_t kVendorRealtek = 0x10ec;

  DEF_SUBFIELD(data, 15, 0, vendor_id);

  static constexpr uint16_t kAddress = 0xfe;
};

struct DacRefLdoControlReg {
  uint16_t data;

  DEF_SUBBIT(data, 9, pow_ldo_dacrefl);  // Power DACREF L LDO
  DEF_SUBBIT(data, 1, pow_ldo_dacrefr);  // Power DACREF R LDO

  static constexpr uint16_t kAddress = 0x112;
};

}  // namespace audio::alc5663

#endif  // SRC_MEDIA_AUDIO_DRIVERS_ALC5663_ALC5663_REGISTERS_H_
