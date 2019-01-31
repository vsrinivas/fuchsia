// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

/**
 * Register definitions taken from
 *
 * MAX98927 10V Boosted Class D Speaker Amplifier with I/V Sense
 * Rev 0.7 (Preliminary)
 */

namespace audio {
namespace max98927 {

constexpr uint16_t INTERRUPT_RAW_1           = 0x0001;
constexpr uint16_t INTERRUPT_RAW_2           = 0x0002;
constexpr uint16_t INTERRUPT_RAW_3           = 0x0003;
constexpr uint16_t INTERRUPT_STATE_1         = 0x0004;
constexpr uint16_t INTERRUPT_STATE_2         = 0x0005;
constexpr uint16_t INTERRUPT_STATE_3         = 0x0006;

constexpr uint16_t PCM_RX_EN_A               = 0x0018;
constexpr uint16_t PCM_RX_EN_B               = 0x0019;
constexpr uint16_t PCM_TX_EN_A               = 0x001A;
constexpr uint16_t PCM_TX_EN_B               = 0x001B;
constexpr uint16_t PCM_TX_HIZ_CTRL_A         = 0x001C;
constexpr uint16_t PCM_TX_HIZ_CTRL_B         = 0x001D;
constexpr uint16_t PCM_TX_CH_SRC_A           = 0x001E;
constexpr uint16_t PCM_TX_CH_SRC_B           = 0x001F;

constexpr uint16_t PCM_MODE_CFG              = 0x0020;
constexpr uint16_t PCM_MASTER_MODE           = 0x0021;
constexpr uint16_t PCM_CLOCK_SETUP           = 0x0022;

constexpr uint16_t PCM_SAMPLE_RATE_SETUP_1   = 0x0023;
constexpr uint16_t PCM_SAMPLE_RATE_SETUP_2   = 0x0024;
constexpr uint16_t PCM_SPK_MONOMIX_A         = 0x0025;
constexpr uint16_t PCM_SPK_MONOMIX_B         = 0x0026;

constexpr uint16_t AMP_VOL_CTRL              = 0x0036;
constexpr uint16_t AMP_DSP_CFG               = 0x0037;
constexpr uint16_t TONE_GEN_DC_CFG           = 0x0038;

constexpr uint16_t AMP_ENABLE                = 0x003A;
constexpr uint16_t SPK_SRC_SEL               = 0x003B;
constexpr uint16_t SPK_GAIN                  = 0x003C;

constexpr uint16_t MEAS_DSP_CFG              = 0x003F;
constexpr uint16_t BOOST_CTRL_0              = 0x0040;
constexpr uint16_t BOOST_CTRL_3              = 0x0041;
constexpr uint16_t BOOST_CTRL_1              = 0x0042;
constexpr uint16_t MEAS_ADC_CFG              = 0x0043;
constexpr uint16_t MEAS_ADC_BASE_DIV_MSB     = 0x0044;
constexpr uint16_t MEAS_ADC_BASE_DIV_LSB     = 0x0045;

constexpr uint16_t BROWNOUT_EN               = 0x0052;

constexpr uint16_t BROWNOUT_LVL4_AMP1_CTRL1  = 0x007F;

constexpr uint16_t ENV_TRACKER_VOUT_HEADROOM = 0x0082;

constexpr uint16_t ENV_TRACKER_CTRL          = 0x0086;
constexpr uint16_t ENV_TRACKER_BOOST_VOUT_RB = 0x0087;

constexpr uint16_t GLOBAL_ENABLE             = 0x00FF;
constexpr uint16_t SOFTWARE_RESET            = 0x0100;
constexpr uint16_t REV_ID                    = 0x01FF;

#define _SIC_ static inline constexpr

// PCM_TX_CH_SRC_A bits
_SIC_ uint8_t PCM_TX_CH_SRC_A_PCM_IVADC_I_DEST(unsigned ch) { return (uint8_t)(ch << 4); }
_SIC_ uint8_t PCM_TX_CH_SRC_A_PCM_IVADC_V_DEST(unsigned ch) { return ch & 0xF; }

// PCM_TX_CH_SRC_B bits
constexpr uint8_t PCM_TX_CH_SRC_B_INTERLEAVE = (1 << 5);

// PCM_SAMPLE_RATE_SETUP_1 bits
_SIC_ uint8_t PCM_SAMPLE_RATE_SETUP_1_DIG_IF_SR(uint8_t val) { return val & 0xF; }

// PCM_SAMPLE_RATE_SETUP_2 bits
_SIC_ uint8_t PCM_SAMPLE_RATE_SETUP_2_SPK_SR(uint8_t val) { return (uint8_t)(val << 4); }
_SIC_ uint8_t PCM_SAMPLE_RATE_SETUP_2_IVADC_SR(uint8_t val) { return val & 0xF; }

// PCM_SPK_MONOMIX_A bits
constexpr uint8_t PCM_SPK_MONOMIX_A_CFG_OUTPUT_0   = 0;
constexpr uint8_t PCM_SPK_MONOMIX_A_CFG_OUTPUT_1   = (1 << 6);
constexpr uint8_t PCM_SPK_MONOMIX_A_CFG_OUTPUT_0_1 = (2 << 6);
_SIC_ uint8_t PCM_SPK_MONOMIX_B_CFG_CH0_SRC(uint8_t ch) { return ch; }

// PCM_SPK_MONOMIX_B bits
_SIC_ uint8_t PCM_SPK_MONOMIX_B_CFG_CH1_SRC(uint8_t ch) { return ch; }

// PCM_MODE_CFG bits
constexpr uint8_t PCM_MODE_CFG_CHANSZ_16BITS    = (1 << 6);
constexpr uint8_t PCM_MODE_CFG_CHANSZ_24BITS    = (2 << 6);
constexpr uint8_t PCM_MODE_CFG_CHANSZ_32BITS    = (3 << 6);
constexpr uint8_t PCM_MODE_CFG_FORMAT_I2S       = (0 << 3);
constexpr uint8_t PCM_MODE_CFG_FORMAT_TDM0      = (3 << 3);
constexpr uint8_t PCM_MODE_CFG_BLKEDGE_FALLING  = (1 << 2);
constexpr uint8_t PCM_MODE_CFG_LRCLKPOL_FALLING = (1 << 1);
constexpr uint8_t PCM_MODE_CFG_TX_EXTRA_HIZ     = (1 << 0);

// AMP_DSP_CFG bits
constexpr uint8_t AMP_DSP_CFG_DCBLK_EN = (1 << 0);

// AMP_ENABLE bits
constexpr uint8_t AMP_ENABLE_EN = (1 << 0);

// SPK_SRC_SEL bits
constexpr uint8_t SPK_SRC_SEL_DAI      = 0;
constexpr uint8_t SPK_SRC_SEL_TONE_GEN = 2;
constexpr uint8_t SPK_SRC_SEL_PDM_IN   = 3;

// SPK_GAIN bits
constexpr uint8_t SPK_GAIN_MUTE = 0;
constexpr uint8_t SPK_GAIN_3DB  = 1;
constexpr uint8_t SPK_GAIN_6DB  = 2;
constexpr uint8_t SPK_GAIN_9DB  = 3;
constexpr uint8_t SPK_GAIN_12DB = 4;
constexpr uint8_t SPK_GAIN_15DB = 5;
constexpr uint8_t SPK_GAIN_18DB = 6;
_SIC_ uint8_t SPK_GAIN_PDM(uint8_t val) { return (uint8_t)(val << 4); }
_SIC_ uint8_t SPK_GAIN_PCM(uint8_t val) { return val; }

// MEAS_DSP_CFG bits
constexpr uint8_t MEAS_DSP_CFG_FREQ_0_06HZ  = 0;
constexpr uint8_t MEAS_DSP_CFG_FREQ_0_118HZ = 1;
constexpr uint8_t MEAS_DSP_CFG_FREQ_0_235HZ = 2;
constexpr uint8_t MEAS_DSP_CFG_FREQ_3_7HZ   = 3;
constexpr uint8_t MEAS_DSP_CFG_DITH_EN      = (1 << 2);
constexpr uint8_t MEAS_DSP_CFG_I_DCBLK_EN   = (1 << 1);
constexpr uint8_t MEAS_DSP_CFG_V_DCBLK_EN   = (1 << 0);
_SIC_ uint8_t MEAS_DSP_CFG_I_DCBLK(uint8_t val) { return (uint8_t)(val << 6); }
_SIC_ uint8_t MEAS_DSP_CFG_V_DCBLK(uint8_t val) { return (uint8_t)(val << 4); }

// MEAS_ADC_CFG bits
// Ch0 = VBAT, Ch1 = VBST, Ch2 = temperature
constexpr uint8_t MEAS_ADC_CFG_CH2_EN = (1 << 2);

// BROWNOUT_EN bits
constexpr uint8_t BROWNOUT_EN_AMP_DSP_EN = (1 << 2);

// ENV_TRACKER_CTRL bits
constexpr uint8_t ENV_TRACKER_CTRL_EN = (1 << 0);

// GLOBAL_ENABLE bits
constexpr uint8_t GLOBAL_ENABLE_EN = (1 << 0);

// SOFTWARE_RESET bits
constexpr uint8_t SOFTWARE_RESET_RST = (1 << 0);

#undef _SIC_

}  // namespace max98927
}  // namespace audio
