// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_AUDIO_REGS_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_AUDIO_REGS_H_

__BEGIN_CDECLS

// clang-format off

//PDM control registers
#define PDM_CTRL                    (0x00 << 2)
#define PDM_HCIC_CTRL1              (0x01 << 2)
#define PDM_HCIC_CTRL2              (0x02 << 2)
#define PDM_F1_CTRL                 (0x03 << 2)
#define PDM_F2_CTRL                 (0x04 << 2)
#define PDM_F3_CTRL                 (0x05 << 2)
#define PDM_HPF_CTRL                (0x06 << 2)
#define PDM_CHAN_CTRL               (0x07 << 2)
#define PDM_CHAN_CTRL1              (0x08 << 2)
#define PDM_COEFF_ADDR              (0x09 << 2)
#define PDM_COEFF_DATA              (0x0a << 2)
#define PDM_CLKG_CTRL               (0x0b << 2)
#define PDM_STS                     (0x0c << 2)


//Clock control registers
#define EE_AUDIO_MCLK_ENA            (1 << 31)

#define EE_AUDIO_CLK_GATE_EN        0x0000

// For version baseline (works with for S905D2G and T931G).
#define EE_AUDIO_MCLK_A_CTRL        0x0004
#define EE_AUDIO_MCLK_B_CTRL        0x0008
#define EE_AUDIO_MCLK_C_CTRL        0x000C
#define EE_AUDIO_MCLK_D_CTRL        0x0010
#define EE_AUDIO_MCLK_E_CTRL        0x0014
#define EE_AUDIO_MCLK_F_CTRL        0x0018
#define EE_AUDIO_MST_PAD_CTRL0      0x001C
#define EE_AUDIO_MST_PAD_CTRL1      0x0020

// For version S905D3G.
#define EE_AUDIO_CLK_GATE_EN1_D3G       0x0004
#define EE_AUDIO_MCLK_A_CTRL_D3G        0x0008
#define EE_AUDIO_MCLK_B_CTRL_D3G        0x000c
#define EE_AUDIO_MCLK_C_CTRL_D3G        0x0010
#define EE_AUDIO_MCLK_D_CTRL_D3G        0x0014
#define EE_AUDIO_MCLK_E_CTRL_D3G        0x0018
#define EE_AUDIO_MCLK_F_CTRL_D3G        0x001c
#define EE_AUDIO_MST_PAD_CTRL0_D3G      0x0020
#define EE_AUDIO_MST_PAD_CTRL1_D3G      0x0024
#define EE_AUDIO_SW_RESET0_D3G          0x0028
#define EE_AUDIO_SW_RESET1_D3G          0x002c

#define EE_AUDIO_MST_A_SCLK_CTRL0     0x0040
#define EE_AUDIO_MST_A_SCLK_CTRL1     0x0044
#define EE_AUDIO_MST_B_SCLK_CTRL0     0x0048
#define EE_AUDIO_MST_B_SCLK_CTRL1     0x004C
#define EE_AUDIO_MST_C_SCLK_CTRL0     0x0050
#define EE_AUDIO_MST_C_SCLK_CTRL1     0x0054
#define EE_AUDIO_MST_D_SCLK_CTRL0     0x0058
#define EE_AUDIO_MST_D_SCLK_CTRL1     0x005C
#define EE_AUDIO_MST_E_SCLK_CTRL0     0x0060
#define EE_AUDIO_MST_E_SCLK_CTRL1     0x0064
#define EE_AUDIO_MST_F_SCLK_CTRL0     0x0068
#define EE_AUDIO_MST_F_SCLK_CTRL1     0x006c

#define EE_AUDIO_CLK_TDMOUT_A_CTL     0x0090
#define EE_AUDIO_CLK_TDMOUT_B_CTL     0x0094
#define EE_AUDIO_CLK_TDMOUT_C_CTL     0x0098

#define EE_AUDIO_CLK_PDMIN_CTRL0      0x00ac
#define EE_AUDIO_CLK_PDMIN_CTRL1      0x00b0

//TODDR control reg blocks and offsets
#define TODDR_CTRL0_OFFS        (0x00 << 2)
#define TODDR_CTRL1_OFFS        (0x01 << 2)
#define TODDR_START_ADDR_OFFS   (0x02 << 2)
#define TODDR_FINISH_ADDR_OFFS  (0x03 << 2)
#define TODDR_INT_ADDR_OFFS     (0x04 << 2)
#define TODDR_STATUS1_OFFS      (0x05 << 2)
#define TODDR_STATUS2_OFFS      (0x06 << 2)
#define TODDR_START_ADDRB_OFFS  (0x07 << 2)
#define TODDR_FINISH_ADDRB_OFFS (0x08 << 2)
#define TODDR_INIT_ADDR_OFFS    (0x09 << 2)
// For version S905D3G.
#define TODDR_CTRL2_OFFS_D3G    (0x0a << 2)

//FRDDR control reg blocks and offsets
#define FRDDR_CTRL0_OFFS        (0x00 << 2)
#define FRDDR_CTRL1_OFFS        (0x01 << 2)
#define FRDDR_START_ADDR_OFFS   (0x02 << 2)
#define FRDDR_FINISH_ADDR_OFFS  (0x03 << 2)
#define FRDDR_INT_ADDR_OFFS     (0x04 << 2)
#define FRDDR_STATUS1_OFFS      (0x05 << 2)
#define FRDDR_STATUS2_OFFS      (0x06 << 2)
#define FRDDR_CTRL2_OFFS_D3G    (0x0a << 2)

#define EE_AUDIO_TODDR_A_CTRL0       (0x40 << 2)
#define EE_AUDIO_TODDR_B_CTRL0       (0x50 << 2)
#define EE_AUDIO_TODDR_C_CTRL0       (0x60 << 2)
#define EE_AUDIO_FRDDR_A_CTRL0       (0x70 << 2)
#define EE_AUDIO_FRDDR_B_CTRL0       (0x80 << 2)
#define EE_AUDIO_FRDDR_C_CTRL0       (0x90 << 2)

#define EE_AUDIO_ARB_CTRL             (0xa0 << 2)

//TDMOUT control regs (common to three separate units)
#define TDMOUT_CTRL0_OFFS     (0x00 << 2)
#define TDMOUT_CTRL1_OFFS     (0x01 << 2)
#define TDMOUT_SWAP_OFFS      (0x02 << 2)
#define TDMOUT_MASK0_OFFS     (0x03 << 2)
#define TDMOUT_MASK1_OFFS     (0x04 << 2)
#define TDMOUT_MASK2_OFFS     (0x05 << 2)
#define TDMOUT_MASK3_OFFS     (0x06 << 2)
#define TDMOUT_STAT_OFFS      (0x07 << 2)
#define TDMOUT_GAIN0_OFFS     (0x08 << 2)
#define TDMOUT_GAIN1_OFFS     (0x09 << 2)
#define TDMOUT_MUTE_VAL_OFFS  (0x0a << 2)
#define TDMOUT_MUTE0_OFFS     (0x0b << 2)
#define TDMOUT_MUTE1_OFFS     (0x0c << 2)
#define TDMOUT_MUTE2_OFFS     (0x0d << 2)
#define TDMOUT_MUTE3_OFFS     (0x0e << 2)
#define TDMOUT_MASK_VAL_OFFS  (0x0f << 2)
#define TDMOUT_CTRL2_OFFS_D3G (0x160 << 2)

#define EE_AUDIO_TDMOUT_A_CTRL0         (0x140 << 2)
#define EE_AUDIO_TDMOUT_B_CTRL0         (0x150 << 2)
#define EE_AUDIO_TDMOUT_C_CTRL0         (0x160 << 2)


//Audio clock gating masks
#define EE_AUDIO_CLK_GATE_ARB        (1 << 0)
#define EE_AUDIO_CLK_GATE_PDM        (1 << 1)
#define EE_AUDIO_CLK_GATE_TDMINA     (1 << 2)
#define EE_AUDIO_CLK_GATE_TDMINB     (1 << 3)
#define EE_AUDIO_CLK_GATE_TDMINC     (1 << 4)
#define EE_AUDIO_CLK_GATE_TDMOUTA    (1 << 6)
#define EE_AUDIO_CLK_GATE_TDMOUTB    (1 << 7)
#define EE_AUDIO_CLK_GATE_TDMOUTC    (1 << 8)
#define EE_AUDIO_CLK_GATE_FRDDRA     (1 << 9)
#define EE_AUDIO_CLK_GATE_FRDDRB     (1 << 10)
#define EE_AUDIO_CLK_GATE_FRDDRC     (1 << 11)
#define EE_AUDIO_CLK_GATE_TODDRA     (1 << 12)
#define EE_AUDIO_CLK_GATE_TODDRB     (1 << 13)
#define EE_AUDIO_CLK_GATE_TODDRC     (1 << 14)



typedef enum {
    MP0_PLL = 0,
    MP1_PLL = 1,
    MP2_PLL = 2,
    MP3_PLL = 3,
    HIFI_PLL = 4,
    FCLK_DIV3 = 5,
    FCLK_DIV4 = 6,
    GP0_PLL = 7
} ee_audio_mclk_src_t;

typedef enum {
    MCLK_A = 0,
    MCLK_B,
    MCLK_C,
    MCLK_D,
    MCLK_E,
    MCLK_F
} aml_tdm_mclk_t;

typedef enum {
    MCLK_PAD_0 = 0,
    MCLK_PAD_1
} aml_tdm_mclk_pad_t;

typedef enum {
    TDM_OUT_A = 0,
    TDM_OUT_B,
    TDM_OUT_C
} aml_tdm_out_t;

typedef enum {
    FRDDR_A = 0,
    FRDDR_B,
    FRDDR_C
} aml_frddr_t;

typedef enum {
    TODDR_A = 0,
    TODDR_B,
    TODDR_C
} aml_toddr_t;

// clang-format on
__END_CDECLS

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_AUDIO_REGS_H_
