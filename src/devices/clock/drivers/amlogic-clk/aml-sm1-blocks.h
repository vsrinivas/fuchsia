// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_CLOCK_DRIVERS_AMLOGIC_CLK_AML_SM1_BLOCKS_H_
#define SRC_DEVICES_CLOCK_DRIVERS_AMLOGIC_CLK_AML_SM1_BLOCKS_H_

#include <soc/aml-meson/sm1-clk.h>

#include "aml-clk-blocks.h"
#include "aml-g12a-blocks.h"

namespace sm1_clk {
constexpr uint32_t kHhiGp1PllCntl0 = 0x18;
constexpr uint32_t kHhiSysCpuClkCntl5 = 0x87;
constexpr uint32_t kHhiSysCpuClkCntl6 = 0x88;

constexpr uint32_t kDosGclkEn0 = (0x3f01 << 2);

}  // namespace sm1_clk

static constexpr meson_clk_gate_t sm1_clk_gates[] = {
    // SYS CPU Clock gates.
    {.reg = kHhiSysCpuClkCntl1, .bit = 24},  // CLK_SYS_PLL_DIV16
    {.reg = kHhiSysCpuClkCntl1, .bit = 1},   // CLK_SYS_CPU_CLK_DIV16

    {.reg = kHhiGclkMpeg0, .bit = 0},   // CLK_DDR
    {.reg = kHhiGclkMpeg0, .bit = 1},   // CLK_DOS
    {.reg = kHhiGclkMpeg0, .bit = 2},   // CLK_ALOCKER
    {.reg = kHhiGclkMpeg0, .bit = 3},   // CLK_MIPI_DSI_HOST
    {.reg = kHhiGclkMpeg0, .bit = 4},   // CLK_ETH_PHY
    {.reg = kHhiGclkMpeg0, .bit = 5},   // CLK_ISA
    {.reg = kHhiGclkMpeg0, .bit = 6},   // CLK_PL301
    {.reg = kHhiGclkMpeg0, .bit = 7},   // CLK_PERIPHS
    {.reg = kHhiGclkMpeg0, .bit = 8},   // CLK_SPICC_0
    {.reg = kHhiGclkMpeg0, .bit = 9},   // CLK_I2C
    {.reg = kHhiGclkMpeg0, .bit = 10},  // CLK_SANA
    {.reg = kHhiGclkMpeg0, .bit = 11},  // CLK_SD
    {.reg = kHhiGclkMpeg0, .bit = 12},  // CLK_RNG0
    {.reg = kHhiGclkMpeg0, .bit = 13},  // CLK_UART0
    {.reg = kHhiGclkMpeg0, .bit = 14},  // CLK_SPICC_1
    {.reg = kHhiGclkMpeg0, .bit = 19},  // CLK_HIU_REG
    {.reg = kHhiGclkMpeg0, .bit = 20},  // CLK_MIPI_DSI_PHY
    {.reg = kHhiGclkMpeg0, .bit = 23},  // CLK_ASSIST_MISC
    {.reg = kHhiGclkMpeg0, .bit = 24},  // CLK_EMMC_A
    {.reg = kHhiGclkMpeg0, .bit = 25},  // CLK_EMMC_B
    {.reg = kHhiGclkMpeg0, .bit = 26},  // CLK_EMMC_C
    {.reg = kHhiGclkMpeg0, .bit = 28},  // CLK_ACODEC

    {.reg = kHhiGclkMpeg1, .bit = 0},   // CLK_AUDIO
    {.reg = kHhiGclkMpeg1, .bit = 3},   // CLK_ETH_CORE
    {.reg = kHhiGclkMpeg1, .bit = 4},   // CLK_DEMUX
    {.reg = kHhiGclkMpeg1, .bit = 11},  // CLK_AIFIFO
    {.reg = kHhiGclkMpeg1, .bit = 13},  // CLK_ADC
    {.reg = kHhiGclkMpeg1, .bit = 16},  // CLK_UART1
    {.reg = kHhiGclkMpeg1, .bit = 20},  // CLK_G2D
    {.reg = kHhiGclkMpeg1, .bit = 23},  // CLK_RESET
    {.reg = kHhiGclkMpeg1, .bit = 24},  // CLK_PCIE_COMB
    {.reg = kHhiGclkMpeg1, .bit = 25},  // CLK_PARSER
    {.reg = kHhiGclkMpeg1, .bit = 26},  // CLK_USB_GENERAL
    {.reg = kHhiGclkMpeg1, .bit = 27},  // CLK_PCIE_PHY
    {.reg = kHhiGclkMpeg1, .bit = 29},  // CLK_AHB_ARB0

    {.reg = kHhiGclkMpeg2, .bit = 1},   // CLK_AHB_DATA_BUS
    {.reg = kHhiGclkMpeg2, .bit = 2},   // CLK_AHB_CTRL_BUS
    {.reg = kHhiGclkMpeg2, .bit = 3},   // CLK_HTX_HDCP22
    {.reg = kHhiGclkMpeg2, .bit = 4},   // CLK_HTX_PCLK
    {.reg = kHhiGclkMpeg2, .bit = 6},   // CLK_BT656
    {.reg = kHhiGclkMpeg2, .bit = 8},   // CLK_USB1_TO_DDR
    {.reg = kHhiGclkMpeg2, .bit = 11},  // CLK_MMC_PCLK
    {.reg = kHhiGclkMpeg2, .bit = 15},  // CLK_UART2
    {.reg = kHhiGclkMpeg2, .bit = 25},  // CLK_VPU_INTR
    {.reg = kHhiGclkMpeg2, .bit = 30},  // CLK_GIC

    {.reg = kHhiGclk2Other, .bit = 1},     // CLK_VCLK2_VENCI0
    {.reg = kHhiGclk2Other, .bit = 2},     // CLK_VCLK2_VENCI1
    {.reg = kHhiGclk2Other, .bit = 3},     // CLK_VCLK2_VENCP0
    {.reg = kHhiGclk2Other, .bit = 4},     // CLK_VCLK2_VENCP1
    {.reg = kHhiGclk2Other, .bit = 5},     // CLK_VCLK2_VENCT0
    {.reg = kHhiGclk2Other, .bit = 6},     // CLK_VCLK2_VENCT1
    {.reg = kHhiGclk2Other, .bit = 7},     // CLK_VCLK2_OTHER
    {.reg = kHhiGclk2Other, .bit = 8},     // CLK_VCLK2_ENCI
    {.reg = kHhiGclk2Other, .bit = 9},     // CLK_VCLK2_ENCP
    {.reg = kHhiGclk2Other, .bit = 10},    // CLK_DAC_CLK
    {.reg = kHhiGclk2Other, .bit = 14},    // CLK_AOCLK_GATE
    {.reg = kHhiGclk2Other, .bit = 16},    // CLK_IEC958_GATE
    {.reg = kHhiGclk2Other, .bit = 20},    // CLK_ENC480P
    {.reg = kHhiGclk2Other, .bit = 21},    // CLK_RNG1
    {.reg = kHhiGclk2Other, .bit = 22},    // CLK_VCLK2_ENCT
    {.reg = kHhiGclk2Other, .bit = 23},    // CLK_VCLK2_ENCL
    {.reg = kHhiGclk2Other, .bit = 24},    // CLK_VCLK2_VENCLMMC
    {.reg = kHhiGclk2Other, .bit = 25},    // CLK_VCLK2_VENCL
    {.reg = kHhiGclk2Other, .bit = 26},    // CLK_VCLK2_OTHER1
    {.reg = kHhiGclkSpMpeg, .bit = 1},     // CLK_EFUSE
    {.reg = kHhiMpegClkCntl, .bit = 7},    // CLK_81
    {.reg = kHhiXtalDivnCntl, .bit = 11},  // CLK_24M
    {.reg = kHhiGenClkCntl, .bit = 11},    // CLK_GEN_CLK
    {.reg = kHhiTsClkCntl, .bit = 8},      // CLK_TS_CLK
    {.reg = kHhiSpiccClkCntl, .bit = 6},   // CLK_SPICC0_GATE
    {.reg = kHhiSpiccClkCntl, .bit = 22},  // CLK_SPICC1_GATE

    {.reg = kDosGclkEn0,
     .bit = 0,
     .register_set = kMesonRegisterSetDos,
     .mask = 0x3ff},  // CLK_DOS_GCLK_VDEC

    // SM1 Specific Clock Gates.
    {.reg = kHhiGclkMpeg1, .bit = 18},  // CLK_CSI_DIG
    {.reg = kHhiGclkMpeg1, .bit = 19},  // CLK_NNA
    {.reg = kHhiGclkMpeg1, .bit = 28},  // CLK_PARSER1
    {.reg = kHhiGclkMpeg2, .bit = 16},  // CLK_CSI_HOST
    {.reg = kHhiGclkMpeg2, .bit = 17},  // CLK_CSI_ADPAT
    {.reg = kHhiGclkMpeg2, .bit = 22},  // CLK_TEMP_SENSOR
    {.reg = kHhiGclkMpeg2, .bit = 29},  // CLK_CSI_PHY
};

static_assert(sm1_clk::CLK_SM1_GATE_COUNT == std::size(sm1_clk_gates),
              "sm1_clk_gates[] and CLK_SM1_COUNT count mismatch");

static constexpr uint32_t kGenClkSelInputs[] = {0, 5, 6, 7, 20, 21, 22, 23, 24, 25, 26, 27, 28};
static constexpr uint32_t kClk81Inputs[] = {6, 5, 7};
static constexpr meson_clk_mux_t sm1_muxes[] = {
    {.reg = kHhiGenClkCntl,
     .mask = 0x1f,
     .shift = 12,
     .n_inputs = 13,
     .inputs = kGenClkSelInputs},  // CLK_GEN_CLK_SEL
    {.reg = kHhiVipnanoqClkCntl,
     .mask = 0x7,
     .shift = 9,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_CTS_VIPNANOQ_CORE_CLK_MUX
    {.reg = kHhiVipnanoqClkCntl,
     .mask = 0x7,
     .shift = 25,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_CTS_VIPNANOQ_AXI_CLK_MUX
    {.reg = sm1_clk::kHhiSysCpuClkCntl5,
     .mask = 0x3,
     .shift = 0,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_DSU_PRE_SRC0
    {.reg = sm1_clk::kHhiSysCpuClkCntl5,
     .mask = 0x3,
     .shift = 16,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_DSU_PRE_SRC1
    {.reg = sm1_clk::kHhiSysCpuClkCntl5,
     .mask = 0x1,
     .shift = 18,
     .n_inputs = 2,
     .inputs = nullptr},  // CLK_DSU_PRE1
    {.reg = sm1_clk::kHhiSysCpuClkCntl5,
     .mask = 0x1,
     .shift = 10,
     .n_inputs = 2,
     .inputs = nullptr},  // CLK_DSU_PRE_POST
    {.reg = sm1_clk::kHhiSysCpuClkCntl5,
     .mask = 0x1,
     .shift = 11,
     .n_inputs = 2,
     .inputs = nullptr},  // CLK_DSU_PRE_CLK
    {.reg = sm1_clk::kHhiSysCpuClkCntl6,
     .mask = 0x1,
     .shift = 27,
     .n_inputs = 2,
     .inputs = nullptr},  // CLK_DSU_CLK
    {.reg = kHhiMpegClkCntl,
     .mask = 0x7,
     .shift = 12,
     .n_inputs = 3,
     .inputs = kClk81Inputs},  // CLK_MPEG_CLK_SEL
};

static_assert(sm1_clk::CLK_SM1_MUX_COUNT == std::size(sm1_muxes),
              "sm1_clk_muxes and CLK_SM1_MUX_COUNT count mismatch");

static constexpr meson_clk_msr_t sm1_clk_msr = {
    .reg0_offset = (0x1 << 2),
    .reg2_offset = (0x3 << 2),
};

static const char* const sm1_clk_table[] = {
    "am_ring_osc_clk_out_ee[0]",
    "am_ring_osc_clk_out_ee[1]",
    "am_ring_osc_clk_out_ee[2]",
    "am_ring_osc_clk_out_ee[3]",
    "gp0_pll_clk",
    "gp1_pll_clk",
    "cts_enci_clk",
    "clk81",
    "cts_encp_clk",
    "cts_encl_clk",
    "cts_vdac_clk",
    "mac_eth_tx_clk",
    "hifi_pll_clk",
    "mod_tcon_clko",
    "cts_FEC_CLK_0",
    "cts_FEC_CLK_1",
    "cts_FEC_CLK_2",
    "sys_pll_div16",
    "sys_cpu_clk_div16",
    "lcd_an_clk_ph2",
    "rtc_osc_clk_out",
    "lcd_an_clk_ph3",
    "mac_eth_phy_ref_clk",
    "mpll_clk_50m",
    "cts_eth_clk125Mhz",
    "cts_eth_clk_rmii",
    "sc_clk_int",
    "co_clkin_to_mac",
    "cts_sar_adc_clk",
    "pcie_clk_inp",
    "pcie_clk_inn",
    "mpll_clk_test_out",
    "cts_vdec_clk",
    "1'b0",
    "eth_mppll_50m_ckout",
    "cts_mali_clk",
    "cts_hdmi_tx_pixel_clk",
    "cts_cdac_clk_c",
    "cts_vdin_meas_clk",
    "cts_bt656_clk0",
    "arm_ring_osc_clk_out[4]",
    "mac_eth_rx_clk_rmii",
    "mp0_clk_out",
    "fclk_div5",
    "cts_pwm_B_clk",
    "cts_pwm_A_clk",
    "cts_vpu_clk",
    "ddr_dpll_pt_clk",
    "mp1_clk_out",
    "mp2_clk_out",
    "mp3_clk_out",
    "cts_sd_emmc_clk_C",
    "cts_sd_emmc_clk_B",
    "cts_sd_emmc_clk_A",
    "cts_vpu_clkc",
    "vid_pll_div_clk_out",
    "cts_wave420l_aclk",
    "cts_wave420l_cclk",
    "cts_wave420l_bclk",
    "cts_hcodec_clk",
    "arm_ring_osc_clk_out[5]",
    "gpio_clk_msr",
    "cts_hevcb_clk",
    "cts_dsi_meas_clk",
    "cts_spicc_1_clk",
    "cts_spicc_0_clk",
    "cts_vid_lock_clk",
    "cts_dsi_phy_clk",
    "cts_hdcp22_esmclk",
    "cts_hdcp22_skpclk",
    "cts_pwm_F_clk",
    "cts_pwm_E_clk",
    "cts_pwm_D_clk",
    "cts_pwm_C_clk",
    "arm_ring_osc_clk_out[6]",
    "cts_hevcf_clk",
    "arm_ring_osc_clk_out[7]",
    "rng_ring_osc_clk[0]",
    "rng_ring_osc_clk[1]",
    "rng_ring_osc_clk[2]",
    "rng_ring_osc_clk[3]",
    "cts_vapbclk",
    "cts_ge2d_clk",
    "co_rx_clk",
    "co_tx_clk",
    "arm_ring_osc_clk_out[8]",
    "arm_ring_osc_clk_out[9]",
    "mipi_csi_phy_clk",
    "csi2_adapt_clk",
    "HDMI_CLK_TODIG",
    "cts_hdmitx_sys_clk",
    "nna_core_clk",
    "nna_axi_clk",
    "vad_clk",
    "eth_phy_rxclk",
    "eth_phy_plltxclk",
    "cts_vpu_clkb",
    "cts_vpu_clkb_tmp",
    "cts_ts_clk",
    "arm_ring_osc_clk_out[10]",
    "arm_ring_osc_clk_out[11]",
    "arm_ring_osc_clk_out[12]",
    "arm_ring_osc_clk_out[13]",
    "arm_ring_osc_clk_out[14]",
    "arm_ring_osc_clk_out[15]",
    "arm_ring_osc_clk_out[16]",
    "ephy_test_clk",
    "au_dac_clk_g128x",
    "c_alocker_in_clk",
    "c_alocker_out_clk",
    "audio_tdmout_c_sclk",
    "audio_tdmout_b_sclk",
    "audio_tdmout_a_sclk",
    "audio_tdmin_lb_sclk",
    "audio_tdmin_c_sclk",
    "audio_tdmin_b_sclk",
    "audio_tdmin_a_sclk",
    "audio_resampleA_clk",
    "audio_pdm_sysclk",
    "audio_spdifout_b_mst_clk",
    "audio_spdifout_mst_clk",
    "audio_spdifin_mst_clk",
    "mod_audio_pdm_dclk_o",
    "audio_resampled_clk",
    "earcx_pll_(dma",
    "earcrx_pll_test_clk",
    "csi_phy0_clk_out",
    "clk_csi2_data",
};

// clang-format off

#endif  // SRC_DEVICES_CLOCK_DRIVERS_AMLOGIC_CLK_AML_SM1_BLOCKS_H_
