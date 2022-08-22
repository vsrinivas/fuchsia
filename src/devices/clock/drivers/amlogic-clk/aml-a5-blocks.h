// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_CLOCK_DRIVERS_AMLOGIC_CLK_AML_A5_BLOCKS_H_
#define SRC_DEVICES_CLOCK_DRIVERS_AMLOGIC_CLK_AML_A5_BLOCKS_H_

#include <soc/aml-meson/a5-clk.h>

#include "aml-clk-blocks.h"

#define CPU_LOW_PARAMS(_rate, _dyn_pre_mux, _dyn_post_mux, _dyn_div)                 \
  {                                                                                  \
    .rate = (_rate), .dyn_pre_mux = (_dyn_pre_mux), .dyn_post_mux = (_dyn_post_mux), \
    .dyn_div = (_dyn_div),                                                           \
  }

#define PLL_PARAMS(_rate, _m, _n, _od) \
  { .rate = (_rate), .m = (_m), .n = (_n), .od = (_od), }

struct cpu_dyn_table {
  uint32_t rate;
  uint16_t dyn_pre_mux;
  uint16_t dyn_post_mux;
  uint16_t dyn_div;
};

struct pll_params_table {
  uint32_t rate;
  uint16_t m;
  uint16_t n;
  uint16_t od;
};

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

// ANA_CTRL
constexpr uint32_t kAnactrlSyspllCtrl0 = ((0x0000 << 2) + 0xfe008000);

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
    .reg0_offset = (0x0 << 2),
    .reg2_offset = (0x2 << 2),
};

// Here the index id is the clock measurement id,
// so we need to add the "__reserved__" field to skip
// some useless ids.
static const char* const a5_clk_table[] = {
    "cts_sys_clk",               // 0
    "cts_axi_clk",               // 1
    "cts_rtc_clk",               // 2
    "cts_dspa_clk",              // 3
    "__reserved__",              // 4
    "__reserved__",              // 5
    "sys_cpu_clk_div16",         // 6
    "__reserved__",              // 7
    "__reserved__",              // 8
    "__reserved__",              // 9
    "fclk_div5",                 // 10
    "mp0_clk_out",               // 11
    "mp1_clk_out",               // 12
    "mp2_clk_out",               // 13
    "mp3_clk_out",               // 14
    "mpll_clk_50m",              // 15
    "sys_oscin32k_i",            // 16
    "rtc_pll_clk",               // 17
    "mpll_clk_test_out",         // 18
    "hifi_pll_clk",              // 19
    "gp0_pll_clk",               // 20
    "gp1_pll_clk",               // 21
    "__reserved__",              // 22
    "sys_pll_div16",             // 23
    "ddr_dpll_pt_clk",           // 24
    "cts_nna_axi_clk",           // 25
    "cts_nna_core_clk",          // 26
    "rtc_sec_pulse_out",         // 27
    "rtc_osc_clk_out",           // 28
    "__reserved__",              // 29
    "mod_eth_phy_ref_clk",       // 30
    "mod_eth_tx_clk",            // 31
    "__reserved__",              // 32
    "__reserved__",              // 33
    "__reserved__",              // 34
    "mod_eth_rx_clk_rmii",       // 35
    "__reserved__",              // 36
    "__reserved__",              // 37
    "__reserved__",              // 38
    "__reserved__",              // 39
    "__reserved__",              // 40
    "__reserved__",              // 41
    "__reserved__",              // 42
    "__reserved__",              // 43
    "__reserved__",              // 44
    "__reserved__",              // 45
    "__reserved__",              // 46
    "__reserved__",              // 47
    "__reserved__",              // 48
    "__reserved__",              // 49
    "__reserved__",              // 50
    "__reserved__",              // 51
    "__reserved__",              // 52
    "__reserved__",              // 53
    "__reserved__",              // 54
    "__reserved__",              // 55
    "__reserved__",              // 56
    "__reserved__",              // 57
    "__reserved__",              // 58
    "__reserved__",              // 59
    "__reserved__",              // 60
    "__reserved__",              // 61
    "__reserved__",              // 62
    "__reserved__",              // 63
    "__reserved__",              // 64
    "__reserved__",              // 65
    "__reserved__",              // 66
    "__reserved__",              // 67
    "__reserved__",              // 68
    "__reserved__",              // 69
    "__reserved__",              // 70
    "__reserved__",              // 71
    "__reserved__",              // 72
    "__reserved__",              // 73
    "__reserved__",              // 74
    "__reserved__",              // 75
    "__reserved__",              // 76
    "__reserved__",              // 77
    "__reserved__",              // 78
    "cts_rama_clk",              // 79
    "__reserved__",              // 80
    "__reserved__",              // 81
    "__reserved__",              // 82
    "__reserved__",              // 83
    "__reserved__",              // 84
    "__reserved__",              // 85
    "__reserved__",              // 86
    "__reserved__",              // 87
    "__reserved__",              // 88
    "__reserved__",              // 89
    "__reserved__",              // 90
    "__reserved__",              // 91
    "__reserved__",              // 92
    "__reserved__",              // 93
    "__reserved__",              // 94
    "__reserved__",              // 95
    "__reserved__",              // 96
    "__reserved__",              // 97
    "__reserved__",              // 98
    "__reserved__",              // 99
    "__reserved__",              // 100
    "__reserved__",              // 101
    "__reserved__",              // 102
    "__reserved__",              // 103
    "__reserved__",              // 104
    "__reserved__",              // 105
    "deskew_pll_clk_div32_out",  // 106
    "__reserved__",              // 107
    "__reserved__",              // 108
    "__reserved__",              // 109
    "__reserved__",              // 110
    "cts_sar_adc_clk",           // 111
    "cts_ts_clk",                // 112
    "cts_sd_emmc_C_clk",         // 113
    "__reserved__",              // 114
    "cts_sd_emmc_A_clk",         // 115
    "gpio_msr_clk",              // 116
    "cts_spicc_1_clk",           // 117
    "cts_spicc_0_clk",           // 118
    "o_mst_sclk_vad",            // 119
    "o_mst_mclk_vad",            // 120
    "o_pdm_sysclk",              // 121
    "mod_audio_pdm_dclk_o",      // 122
    "o_vad_clk",                 // 123
    "audio_mst_clk[0]",          // 124
    "audio_mst_clk[1]",          // 125
    "audio_mst_clk[2]",          // 126
    "audio_mst_clk[3]",          // 127
    "audio_mst_clk[4]",          // 128
    "audio_mst_clk[5]",          // 129
    "audio_mst_clk[6]",          // 130
    "audio_mst_clk[7]",          // 131
    "audio_mst_clk[8]",          // 132
    "audio_mst_clk[9]",          // 133
    "audio_mst_clk[10]",         // 134
    "audio_mst_clk[11]",         // 135
    "audio_mst_clk[12]",         // 136
    "audio_mst_clk[13]",         // 137
    "audio_mst_clk[14]",         // 138
    "audio_mst_clk[15]",         // 139
    "audio_mst_clk[16]",         // 140
    "audio_mst_clk[17]",         // 141
    "audio_mst_clk[18]",         // 142
    "audio_mst_clk[19]",         // 143
    "audio_mst_clk[20]",         // 144
    "audio_mst_clk[21]",         // 145
    "audio_mst_clk[22]",         // 146
    "audio_mst_clk[23]",         // 147
    "audio_mst_clk[24]",         // 148
    "audio_mst_clk[25]",         // 149
    "audio_mst_clk[26]",         // 150
    "audio_mst_clk[27]",         // 151
    "audio_mst_clk[28]",         // 152
    "audio_mst_clk[29]",         // 153
    "audio_mst_clk[30]",         // 154
    "audio_mst_clk[31]",         // 155
    "audio_mst_clk[32]",         // 156
    "audio_mst_clk[33]",         // 157
    "audio_mst_clk[34]",         // 158
    "audio_mst_clk[35]",         // 159
    "__reserved__",              // 160
    "__reserved__",              // 161
    "pwm_h_clk",                 // 162
    "pwm_g_clk",                 // 163
    "pwm_f_clk",                 // 164
    "pwm_e_clk",                 // 165
    "pwm_d_clk",                 // 166
    "pwm_c_clk",                 // 167
    "pwm_b_clk",                 // 168
    "pwm_a_clk",                 // 169
    "__reserved__",              // 170
    "__reserved__",              // 171
    "__reserved__",              // 172
    "__reserved__",              // 173
    "__reserved__",              // 174
    "__reserved__",              // 175
    "rng_ring_osc_clk[0]",       // 176
    "rng_ring_osc_clk[1]",       // 177
    "rng_ring_osc_clk[2]",       // 178
    "rng_ring_osc_clk[3]",       // 179
    "dmc_osc_ring",              // 180
    "dsp_osc_ring",              // 181
    "axi_srama_osc_ring",        // 182
    "nna_osc_ring[0]",           // 183
    "nna_osc_ring[1]",           // 184
    "sys_cpu_osc_ring[0]",       // 185
    "sys_cpu_osc_ring[1]",       // 186
    "sys_cpu_osc_ring[2]",       // 187
    "sys_cpu_osc_ring[3]",       // 188
    "axi_sramb_osc_ring",        // 189
};

static constexpr meson_cpu_clk_t a5_cpu_clks[] = {
    // For A5, we set the clock in secure mode(bl31), not in the driver
    {.initial_hz = 1'200'000'000},
};

static const struct cpu_dyn_table a5_cpu_dyn_table[] = {
    CPU_LOW_PARAMS(24'000'000, 0, 0, 0),    CPU_LOW_PARAMS(100'000'000, 1, 1, 9),
    CPU_LOW_PARAMS(250'000'000, 1, 1, 3),   CPU_LOW_PARAMS(333'333'333, 2, 1, 1),
    CPU_LOW_PARAMS(500'000'000, 1, 1, 1),   CPU_LOW_PARAMS(667'000'000, 2, 0, 0),
    CPU_LOW_PARAMS(1'000'000'000, 1, 0, 0),
};

static const struct pll_params_table a5_sys_pll_params_table[] = {
    PLL_PARAMS(1'200'000'000, 100, 1, 1), PLL_PARAMS(1'404'000'000, 117, 1, 1),
    PLL_PARAMS(1'500'000'000, 125, 1, 1), PLL_PARAMS(1'608'000'000, 67, 1, 0),
    PLL_PARAMS(1'704'000'000, 71, 1, 0),  PLL_PARAMS(1'800'000'000, 75, 1, 0),
    PLL_PARAMS(1'920'000'000, 80, 1, 0),  PLL_PARAMS(2'016'000'000, 84, 1, 0),
};

#undef CPU_LOW_PARAMS
#undef PLL_PARAMS

constexpr uint32_t kSecurePllClk = 0x82000098;
constexpr uint32_t kSecureCpuClk = 0x82000099;

constexpr uint32_t kFinalMuxSelMask = 0x1 << 11;
constexpr uint32_t kFinalMuxSelCpuDyn = 0x0 << 11;
constexpr uint32_t kFinalMuxSelSysPll = 0x1 << 11;

// PLL secure clock index
enum class SecPll {
  kSecidSys0DcoPll = 0,
  kSecidSys0DcoPllDis,
  kSecidSys0PllOd,
  kSecidCpuClkSel,
  kSecidCpuClkRd,
  kSecidCpuClkDyn,
  kSecidDsuPreClkSel,
  kSecidDsuPreClkRd,
  kSecidDsuPreClkDyn,
  kSecidDsuClkSel,
  kSecidDsuClkRd,
  kSecidGp1DcoPll,
  kSecidGp1DcoPllDis,
  kSecidGp1PllOd,
};

#endif  // SRC_DEVICES_CLOCK_DRIVERS_AMLOGIC_CLK_AML_A5_BLOCKS_H_
