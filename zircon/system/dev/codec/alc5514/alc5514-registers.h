// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

/**
 * Register definitions taken from
 *
 * ALC5514 (ALC5514-CG)
 * Voice Digital Signal Processor For Microphone Application Datasheet
 * Rev. 1.3
 * 4 July 2016
 */

namespace audio {
namespace alc5514 {

constexpr uint32_t RESET             = 0x18002000;
constexpr uint32_t PWR_ANA1          = 0x18002004;
constexpr uint32_t PWR_ANA2          = 0x18002008;
constexpr uint32_t I2S_CTRL1         = 0x18002010;
constexpr uint32_t I2S_CTRL2         = 0x18002014;
constexpr uint32_t DIG_IO_CTRL       = 0x18002070;
constexpr uint32_t PAD_CTRL1         = 0x18002080;
constexpr uint32_t DMIC_DATA_CTRL    = 0x180020A0;
constexpr uint32_t DIG_SOURCE_CTRL   = 0x180020A4;
constexpr uint32_t SRC_ENABLE        = 0x180020AC;
constexpr uint32_t CLK_CTRL1         = 0x18002104;
constexpr uint32_t CLK_CTRL2         = 0x18002108;
constexpr uint32_t ASRC_IN_CTRL      = 0x18002180;
constexpr uint32_t DOWNFILTER0_CTRL1 = 0x18002190;
constexpr uint32_t DOWNFILTER0_CTRL2 = 0x18002194;
constexpr uint32_t DOWNFILTER0_CTRL3 = 0x18002198;
constexpr uint32_t DOWNFILTER1_CTRL1 = 0x180021A0;
constexpr uint32_t DOWNFILTER1_CTRL2 = 0x180021A4;
constexpr uint32_t DOWNFILTER1_CTRL3 = 0x180021A8;
constexpr uint32_t ANA_CTRL_LDO10    = 0x18002200;
constexpr uint32_t ANA_CTRL_ADCFED   = 0x18002224;
constexpr uint32_t VERSION_ID        = 0x18002FF0;
constexpr uint32_t DEVICE_ID         = 0x18002FF4;

// RESET bits
constexpr uint32_t RESET_VALUE = 0x000010EC;

// PWR_ANA1 bits
constexpr uint32_t PWR_ANA1_EN_SLEEP_RESET  = (1u << 23);
constexpr uint32_t PWR_ANA1_DMIC_DATA_IN2   = (1u << 15);
constexpr uint32_t PWR_ANA1_POW_CKDET       = (1u << 11);
constexpr uint32_t PWR_ANA1_POW_PLL         = (1u << 7);
constexpr uint32_t PWR_ANA1_POW_LDO18_IN    = (1u << 5);
constexpr uint32_t PWR_ANA1_POW_LDO18_ADC   = (1u << 4);
constexpr uint32_t PWR_ANA1_POW_LDO21       = (1u << 3);
constexpr uint32_t PWR_ANA1_POW_BG_LDO18    = (1u << 2);
constexpr uint32_t PWR_ANA1_POW_BG_LDO21    = (1u << 1);

// PWR_ANA2 bits
constexpr uint32_t PWR_ANA2_POW_PLL2      = (1u << 22);
constexpr uint32_t PWR_ANA2_RSTB_PLL2     = (1u << 21);
constexpr uint32_t PWR_ANA2_POW_PLL2_LDO  = (1u << 20);
constexpr uint32_t PWR_ANA2_POW_PLL1      = (1u << 18);
constexpr uint32_t PWR_ANA2_RSTB_PLL1     = (1u << 17);
constexpr uint32_t PWR_ANA2_POW_PLL1_LDO  = (1u << 16);
constexpr uint32_t PWR_ANA2_POW_BG_MBIAS  = (1u << 15);
constexpr uint32_t PWR_ANA2_POW_MBIAS     = (1u << 14);
constexpr uint32_t PWR_ANA2_POW_VREF2     = (1u << 13);
constexpr uint32_t PWR_ANA2_POW_VREF1     = (1u << 12);
constexpr uint32_t PWR_ANA2_POWR_LDO16    = (1u << 11);
constexpr uint32_t PWR_ANA2_POWL_LDO16    = (1u << 10);
constexpr uint32_t PWR_ANA2_POW_ADC2      = (1u << 9);
constexpr uint32_t PWR_ANA2_POW_INPUT_BUF = (1u << 8);
constexpr uint32_t PWR_ANA2_POW_ADC1_R    = (1u << 7);
constexpr uint32_t PWR_ANA2_POW_ADC1_L    = (1u << 6);
constexpr uint32_t PWR_ANA2_POW2_BSTR     = (1u << 5);
constexpr uint32_t PWR_ANA2_POW2_BSTL     = (1u << 4);
constexpr uint32_t PWR_ANA2_POW_BSTR      = (1u << 3);
constexpr uint32_t PWR_ANA2_POW_BSTL      = (1u << 2);
constexpr uint32_t PWR_ANA2_POW_ADCFEDR   = (1u << 1);
constexpr uint32_t PWR_ANA2_POW_ADCFEDL   = (1u << 0);

// I2S_CTRL1 bits
constexpr uint32_t I2S_CTRL1_MODE_SEL_TDM_MODE  = (1u << 28);
constexpr uint32_t I2S_CTRL1_DATA_FORMAT_PCM_B  = (3u << 16);
constexpr uint32_t I2S_CTRL1_TDMSLOT_SEL_RX_8CH = (3u << 10);
constexpr uint32_t I2S_CTRL1_TDMSLOT_SEL_TX_8CH = (3u << 6);

// I2S_CTRL2 bits
constexpr uint32_t I2S_CTRL2_DOCKING_MODE_ENABLE = (1u << 31);
constexpr uint32_t I2S_CTRL2_DOCKING_MODE_4CH    = (1u << 29);

// DIG_IO_CTRL bits
constexpr uint32_t DIG_IO_CTRL_SEL_GPIO4_I2S_MCLK = (1u << 6);

// DIG_SOURCE_CTRL bits
constexpr uint32_t DIG_SOURCE_CTRL_AD1_INPUT_SEL_DMIC1 = (0 << 1);
constexpr uint32_t DIG_SOURCE_CTRL_AD1_INPUT_SEL_DMIC2 = (1u << 1);
constexpr uint32_t DIG_SOURCE_CTRL_AD1_INPUT_SEL_MASK  = (1u << 1);
constexpr uint32_t DIG_SOURCE_CTRL_AD0_INPUT_SEL_DMIC1 = (0 << 0);
constexpr uint32_t DIG_SOURCE_CTRL_AD0_INPUT_SEL_DMIC2 = (1u << 0);
constexpr uint32_t DIG_SOURCE_CTRL_AD0_INPUT_SEL_MASK  = (1u << 0);

// SRC_ENABLE bits
constexpr uint32_t SRC_ENABLE_SRCOUT_1_INPUT_SEL_PCM_DATA0_LR = (4u << 28);
constexpr uint32_t SRC_ENABLE_SRCOUT_1_INPUT_SEL_MASK         = (0xF << 28);
constexpr uint32_t SRC_ENABLE_SRCOUT_2_INPUT_SEL_PCM_DATA1_LR = (4u << 24);
constexpr uint32_t SRC_ENABLE_SRCOUT_2_INPUT_SEL_MASK         = (0xF << 24);

// CLK_CTRL1 bits
constexpr uint32_t CLK_CTRL1_CLK_AD_ANA1_EN        = (1u << 31);
constexpr uint32_t CLK_CTRL1_CLK_DMIC_OUT2_EN      = (1u << 29);
constexpr uint32_t CLK_CTRL1_CLK_DMIC_OUT1_EN      = (1u << 28);
constexpr uint32_t CLK_CTRL1_CLK_AD1_EN            = (1u << 24);
constexpr uint32_t CLK_CTRL1_CLK_AD0_EN            = (1u << 23);
constexpr uint32_t CLK_CTRL1_CLK_DMIC_OUT_SEL_DIV8 = (3u << 8);
constexpr uint32_t CLK_CTRL1_CLK_DMIC_OUT_SEL_MASK = (0xF << 8);
constexpr uint32_t CLK_CTRL1_CLK_AD_ANA1_SEL_DIV3  = (2u << 0);
constexpr uint32_t CLK_CTRL1_CLK_AD_ANA1_SEL_MASK  = (0xF << 0);

// CLK_CTRL2 bits
constexpr uint32_t CLK_CTRL2_AD1_TRACK                = (1u << 17);
constexpr uint32_t CLK_CTRL2_AD0_TRACK                = (1u << 16);
constexpr uint32_t CLK_CTRL2_CLK_SYS_DIV_OUT_DIV2     = (1u << 8);
constexpr uint32_t CLK_CTRL2_CLK_SYS_DIV_OUT_MASK     = (3u << 8);
constexpr uint32_t CLK_CTRL2_SEL_ADC_OSR_DIV2         = (1u << 4);
constexpr uint32_t CLK_CTRL2_SEL_ADC_OSR_MASK         = (3u << 4);
constexpr uint32_t CLK_CTRL2_CLK_SYS_PRE_SEL_I2S_MCLK = (2u << 0);

// DOWNFILTER_CTRL bits
constexpr uint32_t DOWNFILTER_CTRL_AD_DMIC_MIX_MUTE = (1u << 11);
constexpr uint32_t DOWNFILTER_CTRL_AD_AD_MIX_MUTE   = (1u << 10);
constexpr uint32_t DOWNFILTER_CTRL_AD_AD_MUTE       = (1u << 7);
constexpr uint32_t DOWNFILTER_CTRL_AD_AD_GAIN_MASK  = (0x7F << 0);

// ANA_CTRL_LDO10 bits
constexpr uint32_t ANA_CTRL_LDO10_DLDO_I_LIMIT_EN   = (1u << 16);

// ANA_CTRL_ADCFED bits
constexpr uint32_t ANA_CTRL_ADCFED_BIAS_CTRL_3UA    = (2u << 10);

// DEVICE_ID bits
constexpr uint32_t DEVICE_ID_ALC5514 = 0x10EC5514;

}  // namespace alc5514
}  // namespace audio
