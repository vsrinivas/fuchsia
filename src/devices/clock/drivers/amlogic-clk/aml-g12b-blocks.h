// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_CLOCK_DRIVERS_AMLOGIC_CLK_AML_G12B_BLOCKS_H_
#define SRC_DEVICES_CLOCK_DRIVERS_AMLOGIC_CLK_AML_G12B_BLOCKS_H_

#include <soc/aml-meson/g12b-clk.h>

#include "aml-clk-blocks.h"

// TODO(braval): Use common bitfield header (fxbug.dev/32378) when available for
//               macros used below to avoid duplication.
#define BIT(pos) (1U << (pos))
#define MSR_CLK_SRC_MASK 0x7f
#define MSR_VAL_MASK 0x000FFFFF
#define MSR_CLK_SRC_SHIFT 20
#define MSR_ENABLE BIT(16)
#define MSR_CONT BIT(17)  // continuous measurement.
#define MSR_INTR BIT(18)  // interrupts.
#define MSR_RUN BIT(19)
#define MSR_BUSY BIT(31)

constexpr uint32_t kG12bHhiSysCpuClkCntl1 = (0x57 << 2);
constexpr uint32_t kG12bHhiSysCpubClkCntl1 = (0x80 << 2);
constexpr uint32_t kG12bHhiSysCpubClkCntl = (0x82 << 2);
constexpr uint32_t kG12bHhiTsClkCntl = (0x64 << 2);
constexpr uint32_t kG12bHhiXtalDivnCntl = (0x2f << 2);
constexpr uint32_t kG12bDosGclkEn0 = (0x3f01 << 2);
constexpr uint32_t kG12bHhiGclkMpeg0 = (0x50 << 2);
constexpr uint32_t kG12bHhiGclkMpeg1 = (0x51 << 2);
constexpr uint32_t kG12bHhiGclkMpeg2 = (0x52 << 2);

// NOTE: This list only contains the clocks in use currently and
//       not all available clocks.
static constexpr meson_clk_gate_t g12b_clk_gates[] = {
    // SYS CPU Clock gates.
    {.reg = kG12bHhiSysCpuClkCntl1, .bit = 24},  // G12B_CLK_SYS_PLL_DIV16
    {.reg = kG12bHhiSysCpuClkCntl1, .bit = 1},   // G12B_CLK_SYS_CPU_CLK_DIV16
    {.reg = kG12bHhiXtalDivnCntl, .bit = 11},    // G12B_CLK_CAM_INCK_24M

    // SYS CPUB Clock gates.
    {.reg = kG12bHhiSysCpubClkCntl1, .bit = 24},  // G12B_CLK_SYS_PLLB_DIV16
    {.reg = kG12bHhiSysCpubClkCntl1, .bit = 1},   // G12B_CLK_SYS_CPUB_CLK_DIV16

    {.reg = kG12bDosGclkEn0,
     .bit = 0,
     .register_set = kMesonRegisterSetDos,
     .mask = 0x3ff},  // G12B_CLK_DOS_GCLK_VDEC
    {.reg = kG12bDosGclkEn0,
     .register_set = kMesonRegisterSetDos,
     .mask = 0x7fff << 12},  // G12B_CLK_DOS_GCLK_HCODEC

    // Mpeg0 DOS Clock gate
    {.reg = kG12bHhiGclkMpeg0, .bit = 1},  // G12B_CLK_DOS

    // USB gates
    {.reg = kG12bHhiGclkMpeg1, .bit = 26},  // G12B_CLK_USB
    {.reg = kG12bHhiGclkMpeg2, .bit = 8},   // G12B_CLK_USB1_TO_DDR

    {.reg = kG12bHhiXtalDivnCntl, .bit = 12},  // G12B_CLK_25M
};

static_assert(g12b_clk::CLK_G12B_COUNT == countof(g12b_clk_gates),
              "g12b_clk_gates[] and g12b_clk_gate_idx_t count mismatch");

static meson_clk_msr_t g12b_clk_msr = {
    .reg0_offset = (0x1 << 2),
    .reg2_offset = (0x3 << 2),
};

static constexpr meson_cpu_clk_t g12b_cpu_clks[] = {
    {.reg = kG12bHhiSysCpubClkCntl, .pll = SYS_PLL, .initial_hz = 1'000'000'000},  // Big Cluster
    {.reg = kHhiSysCpuClkCntl0, .pll = SYS1_PLL, .initial_hz = 1'200'000'000},     // Little Cluster
};

// This clock table is meant only for CLK-MEASURE
// Indexes here, correspond to actual clk mux
// values written to measure respective clk.
static const char* const g12b_clk_table[] = {
    "am_ring_osc_clk_out_ee[0]",  //  0
    "am_ring_osc_clk_out_ee[1]",  //  1
    "am_ring_osc_clk_out_ee[2]",  //  2
    "sys_cpu_ring_osc_clk[0]",    //  3
    "gp0_pll_clk",                //  4
    "1'b0",                       //  5
    "cts_enci_clk",               //  6
    "clk81",                      //  7
    "cts_encp_clk",               //  8
    "cts_encl_clk",               //  9
    "cts_vdac_clk",               // 10
    "mac_eth_tx_clk",             // 11
    "hifi_pll_clk",               // 12
    "mod_tcon_clko",              // 13
    "cts_FEC_CLK_0",              // 14
    "cts_FEC_CLK_1",              // 15
    "cts_FEC_CLK_2",              // 16
    "sys_pll_div16",              // 17
    "sys_cpu_clk_div16",          // 18
    "lcd_an_clk_ph2",             // 19
    "rtc_osc_clk_out",            // 20
    "lcd_an_clk_ph3",             // 21
    "mac_eth_phy_ref_clk",        // 22
    "mpll_clk_50m",               // 23
    "cts_eth_clk125Mhz",          // 24
    "cts_eth_clk_rmii",           // 25
    "sc_clk_int",                 // 26
    "co_clkin_to_mac",            // 27
    "cts_sar_adc_clk",            // 28
    "pcie_clk_inp",               // 29
    "pcie_clk_inn",               // 30
    "mpll_clk_test_out",          // 31
    "cts_vdec_clk",               // 32
    "sys_cpu_ring_osc_clk[1]",    // 33
    "eth_mppll_50m_ckout",        // 34
    "cts_mali_clk",               // 35
    "cts_hdmi_tx_pixel_clk",      // 36
    "cts_cdac_clk_c",             // 37
    "cts_vdin_meas_clk",          // 38
    "cts_bt656_clk0",             // 39
    "1'b0",                       // 40
    "mac_eth_rx_clk_rmii",        // 41
    "mp0_clk_out",                // 42
    "fclk_div5",                  // 43
    "cts_pwm_B_clk",              // 44
    "cts_pwm_A_clk",              // 45
    "cts_vpu_clk",                // 46
    "ddr_dpll_pt_clk",            // 47
    "mp1_clk_out",                // 48
    "mp2_clk_out",                // 49
    "mp3_clk_out",                // 50
    "cts_sd_emmc_clk_C",          // 51
    "cts_sd_emmc_clk_B",          // 52
    "cts_sd_emmc_clk_A",          // 53
    "cts_vpu_clkc",               // 54
    "vid_pll_div_clk_out",        // 55
    "cts_wave420l_aclk",          // 56
    "cts_wave420l_cclk",          // 57
    "cts_wave420l_bclk",          // 58
    "cts_hcodec_clk",             // 59
    "1'b0",                       // 60
    "gpio_clk_msr",               // 61
    "cts_hevcb_clk",              // 62
    "cts_dsi_meas_clk",           // 63
    "cts_spicc_1_clk",            // 64
    "cts_spicc_0_clk",            // 65
    "cts_vid_lock_clk",           // 66
    "cts_dsi_phy_clk",            // 67
    "cts_hdcp22_esmclk",          // 68
    "cts_hdcp22_skpclk",          // 69
    "cts_pwm_F_clk",              // 70
    "cts_pwm_E_clk",              // 71
    "cts_pwm_D_clk",              // 72
    "cts_pwm_C_clk",              // 73
    "1'b0",                       // 74
    "cts_hevcf_clk",              // 75
    "1'b0",                       // 76
    "rng_ring_osc_clk[0]",        // 77
    "rng_ring_osc_clk[1]",        // 78
    "rng_ring_osc_clk[2]",        // 79
    "rng_ring_osc_clk[3]",        // 80
    "cts_vapbclk",                // 81
    "cts_ge2d_clk",               // 82
    "co_rx_clk",                  // 83
    "co_tx_clk",                  // 84
    "1'b0",                       // 85
    "1'b0",                       // 86
    "1'b0",                       // 87
    "1'b0",                       // 88
    "HDMI_CLK_TODIG",             // 89
    "cts_hdmitx_sys_clk",         // 90
    "sys_cpuB_clk_div16",         // 91
    "sys_pllB_div16",             // 92
    "1'b0",                       // 93
    "eth_phy_rxclk",              // 94
    "eth_phy_plltxclk",           // 95
    "cts_vpu_clkb",               // 96
    "cts_vpu_clkb_tmp",           // 97
    "cts_ts_clk",                 // 98
    "am_ring_osc_clk_out_ee[3]",  // 99
    "am_ring_osc_clk_out_ee[4]",  // 100
    "am_ring_osc_clk_out_ee[5]",  // 101
    "am_ring_osc_clk_out_ee[6]",  // 102
    "am_ring_osc_clk_out_ee[7]",  // 103
    "am_ring_osc_clk_out_ee[8]",  // 104
    "am_ring_osc_clk_out_ee[9]",  // 105
    "ephy_test_clk",              // 106
    "au_dac_clk_g128x",           // 107
    "c_alocker_in_clk",           // 108
    "c_alocker_out_clk",          // 109
    "audio_tdmout_c_sclk",        // 110
    "audio_tdmout_b_sclk",        // 111
    "audio_tdmout_a_sclk",        // 112
    "audio_tdmin_lb_sclk",        // 113
    "audio_tdmin_c_sclk",         // 114
    "audio_tdmin_b_sclk",         // 115
    "audio_tdmin_a_sclk",         // 116
    "audio_resample_clk",         // 117
    "audio_pdm_sysclk",           // 118
    "audio_spdifout_b_mst_clk",   // 119
    "audio_spdifout_mst_clk",     // 120
    "audio_spdifin_mst_clk",      // 121
    "mod_audio_pdm_dclk_o",       // 122
    "cts_gdc_axi_clk",            // 123
    "cts_gdc_core_clk",           // 124
    "mipi_csi_phy0_clk_out",      // 125
    "mipi_csi_phy1_clk_out",      // 126
};

#endif  // SRC_DEVICES_CLOCK_DRIVERS_AMLOGIC_CLK_AML_G12B_BLOCKS_H_
