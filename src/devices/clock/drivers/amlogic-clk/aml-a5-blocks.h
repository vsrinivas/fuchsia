// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_CLOCK_DRIVERS_AMLOGIC_CLK_AML_A5_BLOCKS_H_
#define SRC_DEVICES_CLOCK_DRIVERS_AMLOGIC_CLK_AML_A5_BLOCKS_H_

#include <soc/aml-meson/a5-clk.h>

#include "aml-clk-blocks.h"

constexpr uint32_t kA5ClkctrlOscinCtrl = (0x1 << 2);
constexpr uint32_t kA5ClkctrlRtcByOscinCtrl0 = (0x2 << 2);
constexpr uint32_t kA5ClkctrlRtcCtrl = (0x4 << 2);
constexpr uint32_t kA5ClkctrlSysClkCtrl0 = (0x10 << 2);
constexpr uint32_t kA5ClkctrlAxiClkCtrl0 = (0x1b << 2);
constexpr uint32_t kA5ClkctrlRamaClkCtrl0 = (0x29 << 2);
constexpr uint32_t kA5ClkctrlDspaClkCtrl0 = (0x27 << 2);
constexpr uint32_t kA5ClkctrlClk1224Ctrl = (0x2a << 2);
constexpr uint32_t kA5ClkctrlEthClkCtrl = (0x59 << 2);
constexpr uint32_t kA5ClkctrlTsClkCtrl = (0x56 << 2);
constexpr uint32_t kA5ClkctrlNandClkCtrl = (0x5a << 2);
constexpr uint32_t kA5ClkctrlSdEmmcClkCtrl = (0x5b << 2);
constexpr uint32_t kA5ClkctrlSpiccClkCtrl = (0x5d << 2);
constexpr uint32_t kA5ClkctrlPwmClkABCtrl = (0x60 << 2);
constexpr uint32_t kA5ClkctrlPwmClkCDCtrl = (0x61 << 2);
constexpr uint32_t kA5ClkctrlPwmClkEFCtrl = (0x62 << 2);
constexpr uint32_t kA5ClkctrlPwmClkGHCtrl = (0x63 << 2);
constexpr uint32_t kA5ClkctrlSarClkCtrl0 = (0x5f << 2);
constexpr uint32_t kA5ClkctrlGenClkCtrl = (0x5e << 2);
constexpr uint32_t kA5ClkctrlNnaClkCtrl = (0x88 << 2);
constexpr uint32_t kA5ClkctrlTimestampCtrl = (0x100 << 2);
constexpr uint32_t kA5ClkctrlTimebaseCtrl0 = (0x106 << 2);
constexpr uint32_t kA5ClkctrlTimebaseCtrl1 = (0x107 << 2);

// clang-format off
static constexpr meson_clk_gate_t a5_clk_gates[] = {
    {.reg = kA5ClkctrlOscinCtrl, .bit = 9},         // CLK_USB_CTRL
    {.reg = kA5ClkctrlOscinCtrl, .bit = 6},         // CLK_USB_PLL
    {.reg = kA5ClkctrlOscinCtrl, .bit = 4},         // CLK_PLL_TOP
    {.reg = kA5ClkctrlOscinCtrl, .bit = 2},         // CLK_DDR_PHY
    {.reg = kA5ClkctrlOscinCtrl, .bit = 1},         // CLK_DDR_PLL
    {.reg = kA5ClkctrlRtcByOscinCtrl0, .bit = 31},  // CLK_RTC_IN
    {.reg = kA5ClkctrlRtcByOscinCtrl0, .bit = 30},  // CLK_RTC_OUT
    {.reg = kA5ClkctrlSysClkCtrl0, .bit = 13},      // CLK_SYS_PRE_A
    {.reg = kA5ClkctrlSysClkCtrl0, .bit = 29},      // CLK_SYS_RRE_B
    {.reg = kA5ClkctrlAxiClkCtrl0, .bit = 13},      // CLK_AXI_PRE_A
    {.reg = kA5ClkctrlAxiClkCtrl0, .bit = 29},      // CLK_AXI_PRE_B
    {.reg = kA5ClkctrlRamaClkCtrl0, .bit = 13},     // CLK_RAMA_PRE_A
    {.reg = kA5ClkctrlRamaClkCtrl0, .bit = 29},     // CLK_RAMA_PRE_B
    {.reg = kA5ClkctrlDspaClkCtrl0, .bit = 13},     // CLK_DSPA_PRE_A
    {.reg = kA5ClkctrlDspaClkCtrl0, .bit = 29},     // CLK_DSPA_PRE_B
    {.reg = kA5ClkctrlClk1224Ctrl, .bit = 10},      // CLK_CLK24_DIV2
    {.reg = kA5ClkctrlClk1224Ctrl, .bit = 11},      // CLK_CLK24
    {.reg = kA5ClkctrlClk1224Ctrl, .bit = 12},      // CLK_CLK25
    {.reg = kA5ClkctrlEthClkCtrl, .bit = 7},        // CLK_ETH_125M
    {.reg = kA5ClkctrlEthClkCtrl, .bit = 8},        // CLK_ETH_RMII
    {.reg = kA5ClkctrlTsClkCtrl, .bit = 8},         // CLK_TS
    {.reg = kA5ClkctrlNandClkCtrl, .bit = 7},       // CLK_NAND
    {.reg = kA5ClkctrlSdEmmcClkCtrl, .bit = 7},     // CLK_SD_EMMC_A
    {.reg = kA5ClkctrlSpiccClkCtrl, .bit = 6},      // CLK_SPICC_0
    {.reg = kA5ClkctrlSpiccClkCtrl, .bit = 22},     // CLK_SPICC_1
    {.reg = kA5ClkctrlPwmClkABCtrl, .bit = 8},      // CLK_PWM_A
    {.reg = kA5ClkctrlPwmClkABCtrl, .bit = 24},     // CLK_PWM_8
    {.reg = kA5ClkctrlPwmClkCDCtrl, .bit = 8},      // CLK_PWM_A
    {.reg = kA5ClkctrlPwmClkCDCtrl, .bit = 24},     // CLK_PWM_8
    {.reg = kA5ClkctrlPwmClkEFCtrl, .bit = 8},      // CLK_PWM_E
    {.reg = kA5ClkctrlPwmClkEFCtrl, .bit = 24},     // CLK_PWM_F
    {.reg = kA5ClkctrlPwmClkGHCtrl, .bit = 8},      // CLK_PWM_G
    {.reg = kA5ClkctrlPwmClkGHCtrl, .bit = 24},     // CLK_PWM_H
    {.reg = kA5ClkctrlSarClkCtrl0, .bit = 8},       // CLK_ADC
    {.reg = kA5ClkctrlGenClkCtrl, .bit = 11},       // CLK_GEN
    {.reg = kA5ClkctrlNnaClkCtrl, .bit = 8},        // CLK_NNA_CORE
    {.reg = kA5ClkctrlNnaClkCtrl, .bit = 24},       // CLK_NNA_AXI
    {.reg = kA5ClkctrlTimestampCtrl, .bit = 9},     // CLK_TIMESTAMP
};

static_assert(a5_clk::CLK_A5_GATE_COUNT == std::size(a5_clk_gates),
              "a5_clk_gates[] and CLK_A5_COUNT count mismatch");

static constexpr meson_clk_mux_t a5_muxes[] = {
    {.reg = kA5ClkctrlOscinCtrl,
     .mask = 0x1f,
     .shift = 31,
     .n_inputs = 2,
     .inputs = nullptr},  // CLK_OSC_SEL
    {.reg = kA5ClkctrlRtcCtrl,
     .mask = 0x3,
     .shift = 0,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_RTC_SEL
    {.reg = kA5ClkctrlSysClkCtrl0,
     .mask = 0x7,
     .shift = 10,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_SYS_PRE_A_SEL
    {.reg = kA5ClkctrlSysClkCtrl0,
     .mask = 0x7,
     .shift = 26,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_SYS_PRE_B_SEL
    {.reg = kA5ClkctrlAxiClkCtrl0,
     .mask = 0x7,
     .shift = 10,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_AXI_PRE_A_SEL
    {.reg = kA5ClkctrlAxiClkCtrl0,
     .mask = 0x7,
     .shift = 26,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_AXI_PRE_B_SEL
    {.reg = kA5ClkctrlRamaClkCtrl0,
     .mask = 0x7,
     .shift = 10,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_RAMA_PRE_A_SEL
    {.reg = kA5ClkctrlRamaClkCtrl0,
     .mask = 0x7,
     .shift = 26,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_RAMA_PRE_B_SEL
    {.reg = kA5ClkctrlDspaClkCtrl0,
     .mask = 0x7,
     .shift = 10,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_DSPA_PRE_A_SEL
    {.reg = kA5ClkctrlDspaClkCtrl0,
     .mask = 0x7,
     .shift = 26,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_DSPA_PRE_B_SEL
    {.reg = kA5ClkctrlEthClkCtrl,
     .mask = 0x7,
     .shift = 9,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_ETH_RMII_SEL
    {.reg = kA5ClkctrlNandClkCtrl,
     .mask = 0x7,
     .shift = 9,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_NAND_SEL
    {.reg = kA5ClkctrlSdEmmcClkCtrl,
     .mask = 0x7,
     .shift = 9,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_SD_EMMCA_SEL
    {.reg = kA5ClkctrlSpiccClkCtrl,
     .mask = 0x7,
     .shift = 7,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_SPICC0_SEL
    {.reg = kA5ClkctrlSpiccClkCtrl,
     .mask = 0x7,
     .shift = 23,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_SPICC1_SEL
    {.reg = kA5ClkctrlPwmClkABCtrl,
     .mask = 0x3,
     .shift = 9,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_PWM_A_SEL
    {.reg = kA5ClkctrlPwmClkABCtrl,
     .mask = 0x3,
     .shift = 25,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_PWM_B_SEL
    {.reg = kA5ClkctrlPwmClkCDCtrl,
     .mask = 0x3,
     .shift = 9,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_PWM_C_SEL
    {.reg = kA5ClkctrlPwmClkCDCtrl,
     .mask = 0x3,
     .shift = 25,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_PWM_D_SEL
    {.reg = kA5ClkctrlPwmClkEFCtrl,
     .mask = 0x3,
     .shift = 9,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_PWM_E_SEL
    {.reg = kA5ClkctrlPwmClkEFCtrl,
     .mask = 0x3,
     .shift = 25,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_PWM_F_SEL
    {.reg = kA5ClkctrlPwmClkGHCtrl,
     .mask = 0x3,
     .shift = 9,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_PWM_G_SEL
    {.reg = kA5ClkctrlPwmClkGHCtrl,
     .mask = 0x3,
     .shift = 25,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_PWM_H_SEL
    {.reg = kA5ClkctrlSarClkCtrl0,
     .mask = 0x3,
     .shift = 9,
     .n_inputs = 2,
     .inputs = nullptr},  // CLK_ADC_SEL
    {.reg = kA5ClkctrlGenClkCtrl,
     .mask = 0x1f,
     .shift = 12,
     .n_inputs = 32,
     .inputs = nullptr},  // CLK_GEN_SEL
    {.reg = kA5ClkctrlNnaClkCtrl,
     .mask = 0x7,
     .shift = 9,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_NNA_CORE_SEL
    {.reg = kA5ClkctrlNnaClkCtrl,
     .mask = 0x7,
     .shift = 25,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_NNA_AXI_SEL
    {.reg = kA5ClkctrlTimestampCtrl,
     .mask = 0x3,
     .shift = 10,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_TIMESTAMP_SEL
};
// clang-format on

static_assert(a5_clk::CLK_A5_MUX_COUNT == std::size(a5_muxes),
              "a5_muxes and CLK_A5_MUX_COUNT count mismatch");

static constexpr meson_clk_msr_t a5_clk_msr = {
    .reg0_offset = (0x1 << 2),
    .reg2_offset = (0x3 << 2),
};

static const char* const a5_clk_table[] = {
    "cts_sys_clk",
    "cts_axi_clk",
    "cts_rtc_clk",
    "cts_dspa_clk",
    "sys_cpu_clk_div16",
    "fclk_div5",
    "mp0_clk_out",
    "mp1_clk_out",
    "mp2_clk_out",
    "mp3_clk_out",
    "mpll_clk_50m",
    "sys_oscin32k_i",
    "rtc_pll_clk",
    "mpll_clk_test_out",
    "hifi_pll_clk",
    "gp0_pll_clk",
    "gp1_pll_clk",
    "sys_pll_div16",
    "ddr_dpll_pt_clk",
    "cts_nna_axi_clk",
    "cts_nna_core_clk",
    "rtc_sec_pulse_out",
    "rtc_osc_clk_out",
    "mod_eth_phy_ref_clk",
    "mod_eth_tx_clk",
    "mod_eth_rx_clk_rmii",
    "cts_rama_clk",
    "deskew_pll_clk_div32_out",
    "cts_sar_adc_clk",
    "cts_ts_clk",
    "cts_sd_emmc_C_clk",
    "cts_sd_emmc_A_clk",
    "gpio_msr_clk",
    "cts_spicc_1_clk",
    "cts_spicc_0_clk",
    "o_mst_sclk_vad",
    "o_mst_mclk_vad",
    "o_pdm_sysclk",
    "mod_audio_pdm_dclk_o",
    "o_vad_clk",
    "audio_mst_clk[0]",
    "audio_mst_clk[1]",
    "audio_mst_clk[2]",
    "audio_mst_clk[3]",
    "audio_mst_clk[4]",
    "audio_mst_clk[5]",
    "audio_mst_clk[6]",
    "audio_mst_clk[7]",
    "audio_mst_clk[8]",
    "audio_mst_clk[9]",
    "audio_mst_clk[10]",
    "audio_mst_clk[11]",
    "audio_mst_clk[12]",
    "audio_mst_clk[13]",
    "audio_mst_clk[14]",
    "audio_mst_clk[15]",
    "audio_mst_clk[16]",
    "audio_mst_clk[17]",
    "audio_mst_clk[18]",
    "audio_mst_clk[19]",
    "audio_mst_clk[20]",
    "audio_mst_clk[21]",
    "audio_mst_clk[22]",
    "audio_mst_clk[23]",
    "audio_mst_clk[24]",
    "audio_mst_clk[25]",
    "audio_mst_clk[26]",
    "audio_mst_clk[27]",
    "audio_mst_clk[28]",
    "audio_mst_clk[29]",
    "audio_mst_clk[30]",
    "audio_mst_clk[31]",
    "audio_mst_clk[32]",
    "audio_mst_clk[33]",
    "audio_mst_clk[34]",
    "audio_mst_clk[35]",
    "pwm_h_clk",
    "pwm_g_clk",
    "pwm_f_clk",
    "pwm_e_clk",
    "pwm_d_clk",
    "pwm_c_clk",
    "pwm_b_clk",
    "pwm_a_clk",
    "rng_ring_osc_clk[0]",
    "rng_ring_osc_clk[1]",
    "rng_ring_osc_clk[2]",
    "rng_ring_osc_clk[3]",
    "dmc_osc_ring",
    "dsp_osc_ring",
    "axi_srama_osc_ring",
    "nna_osc_ring[0]",
    "nna_osc_ring[1]",
    "sys_cpu_osc_ring[0]",
    "sys_cpu_osc_ring[1]",
    "sys_cpu_osc_ring[2]",
    "sys_cpu_osc_ring[3]",
    "axi_sramb_osc_ring",
};

#endif  // SRC_DEVICES_CLOCK_DRIVERS_AMLOGIC_CLK_AML_A5_BLOCKS_H_
