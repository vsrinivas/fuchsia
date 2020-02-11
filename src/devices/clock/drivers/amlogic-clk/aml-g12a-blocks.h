// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "aml-clk-blocks.h"
#include <soc/aml-meson/g12a-clk.h>

// TODO(braval): Use common bitfield header (ZX-2526) when available for
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

// clang-format off
constexpr uint32_t kHhiMipiCntl0             = (0x00 << 2);
constexpr uint32_t kHhiMipiCntl1             = (0x01 << 2);
constexpr uint32_t kHhiMipiCntl2             = (0x02 << 2);
constexpr uint32_t kHhiMipiSts               = (0x03 << 2);
constexpr uint32_t kHhiCheckClkResult        = (0x04 << 2);
constexpr uint32_t kScrSystemClkReference    = (0x0B << 2);
constexpr uint32_t kTimeoutValue             = (0x0F << 2);
constexpr uint32_t kHhiGp0PllCntl0           = (0x10 << 2);
constexpr uint32_t kHhiGp0PllCntl1           = (0x11 << 2);
constexpr uint32_t kHhiGp0PllCntl2           = (0x12 << 2);
constexpr uint32_t kHhiGp0PllCntl3           = (0x13 << 2);
constexpr uint32_t kHhiGp0PllCntl4           = (0x14 << 2);
constexpr uint32_t kHhiGp0PllCntl5           = (0x15 << 2);
constexpr uint32_t kHhiGp0PllCntl6           = (0x16 << 2);
constexpr uint32_t kHhiGp0PllSts             = (0x17 << 2);
constexpr uint32_t kHhiPciePllCntl0          = (0x26 << 2);
constexpr uint32_t kHhiPciePllCntl1          = (0x27 << 2);
constexpr uint32_t kHhiPciePllCntl2          = (0x28 << 2);
constexpr uint32_t kHhiPciePllCntl3          = (0x29 << 2);
constexpr uint32_t kHhiPciePllCntl4          = (0x2A << 2);
constexpr uint32_t kHhiPciePllCntl5          = (0x2B << 2);
constexpr uint32_t kHhiPciePllSts            = (0x2E << 2);
constexpr uint32_t kHhiXtalDivnCntl          = (0x2f << 2);
constexpr uint32_t kHhiGclk2Mpeg0            = (0x30 << 2);
constexpr uint32_t kHhiGclk2Mpeg1            = (0x31 << 2);
constexpr uint32_t kHhiGclk2Mpeg2            = (0x32 << 2);
constexpr uint32_t kHhiGclk2Other            = (0x34 << 2);
constexpr uint32_t kHhiHifiPllCntl0          = (0x36 << 2);
constexpr uint32_t kHhiHifiPllCntl1          = (0x37 << 2);
constexpr uint32_t kHhiHifiPllCntl2          = (0x38 << 2);
constexpr uint32_t kHhiHifiPllCntl3          = (0x39 << 2);
constexpr uint32_t kHhiHifiPllCntl4          = (0x3A << 2);
constexpr uint32_t kHhiHifiPllCntl5          = (0x3B << 2);
constexpr uint32_t kHhiHifiPllCntl6          = (0x3C << 2);
constexpr uint32_t kHhiHifiPllSts            = (0x3D << 2);
constexpr uint32_t kHhiTimer90k              = (0x3F << 2);
constexpr uint32_t kHhiMemPdReg0             = (0x40 << 2);
constexpr uint32_t kHhiVpuMemPdReg0          = (0x41 << 2);
constexpr uint32_t kHhiVpuMemPdReg1          = (0x42 << 2);
constexpr uint32_t kHhiViidClkDiv            = (0x4a << 2);
constexpr uint32_t kHhiViidClkCntl           = (0x4b << 2);
constexpr uint32_t kHhiVpuMemPdReg2          = (0x4d << 2);
constexpr uint32_t kHhiGclkLock              = (0x4F << 2);
constexpr uint32_t kHhiGclkMpeg0             = (0x50 << 2);
constexpr uint32_t kHhiGclkMpeg1             = (0x51 << 2);
constexpr uint32_t kHhiGclkMpeg2             = (0x52 << 2);
constexpr uint32_t kHhiGclkOther             = (0x54 << 2);
constexpr uint32_t kHhiGclkSpMpeg            = (0x55 << 2);
constexpr uint32_t kHhiSysCpuClkCntl1        = (0x57 << 2);
constexpr uint32_t kHhiSysCpuResetCntl       = (0x58 << 2);
constexpr uint32_t kHhiVidClkDiv             = (0x59 << 2);
constexpr uint32_t kHhiMpegClkCntl           = (0x5d << 2);
constexpr uint32_t kHhiVidClkCntl            = (0x5f << 2);
constexpr uint32_t kHhiTsClkCntl             = (0x64 << 2);
constexpr uint32_t kHhiVidClkCntl2           = (0x65 << 2);
constexpr uint32_t kHhiSysCpuClkCntl0        = (0x67 << 2);
constexpr uint32_t kHhiVidPllClkDiv          = (0x68 << 2);
constexpr uint32_t kHhiDvalinClkCntl         = (0x6c << 2);
constexpr uint32_t kHhiVpuClkcCntl           = (0x6D << 2);
constexpr uint32_t kHhiVpuClkCntl            = (0x6F << 2);
constexpr uint32_t kHhiVipnanoqClkCntl       = (0x72 << 2);
constexpr uint32_t kHhiHdmiClkCntl           = (0x73 << 2);
constexpr uint32_t kHhiEthClkCntl            = (0x76 << 2);
constexpr uint32_t kHhiVdecClkCntl           = (0x78 << 2);
constexpr uint32_t kHhiVdec2ClkCntl          = (0x79 << 2);
constexpr uint32_t kHhiVdec3ClkCntl          = (0x7a << 2);
constexpr uint32_t kHhiVdec4ClkCntl          = (0x7b << 2);
constexpr uint32_t kHhiHdcp22ClkCntl         = (0x7c << 2);
constexpr uint32_t kHhiVapbclkCntl           = (0x7d << 2);
constexpr uint32_t kHhiVpuClkbCntl           = (0x83 << 2);
constexpr uint32_t kHhiGenClkCntl            = (0x8a << 2);
constexpr uint32_t kHhiVdinMeasClkCntl       = (0x94 << 2);
constexpr uint32_t kHhiMipidsiPhyClkCntl     = (0x95 << 2);
constexpr uint32_t kHhiNandClkCntl           = (0x97 << 2);
constexpr uint32_t kHhiSdEmmcClkCntl         = (0x99 << 2);
constexpr uint32_t kHhiWave420lClkCntl       = (0x9A << 2);
constexpr uint32_t kHhiWave420lClkCntl2      = (0x9B << 2);
constexpr uint32_t kHhiMpllCntl0             = (0x9E << 2);
constexpr uint32_t kHhiMpllCntl1             = (0x9F << 2);
constexpr uint32_t kHhiMpllCntl2             = (0xA0 << 2);
constexpr uint32_t kHhiMpllCntl3             = (0xA1 << 2);
constexpr uint32_t kHhiMpllCntl4             = (0xA2 << 2);
constexpr uint32_t kHhiMpllCntl5             = (0xA3 << 2);
constexpr uint32_t kHhiMpllCntl6             = (0xA4 << 2);
constexpr uint32_t kHhiMpllCntl7             = (0xA5 << 2);
constexpr uint32_t kHhiMpllCntl8             = (0xA6 << 2);
constexpr uint32_t kHhiMpllCntlSts           = (0xA7 << 2);
constexpr uint32_t kHhiFixPllCntl0           = (0xA8 << 2);
constexpr uint32_t kHhiFixPllCntl1           = (0xA9 << 2);
constexpr uint32_t kHhiFixPllCntl2           = (0xAA << 2);
constexpr uint32_t kHhiFixPllCntl3           = (0xAB << 2);
constexpr uint32_t kHhiFixPllCntl4           = (0xAC << 2);
constexpr uint32_t kHhiFixPllCntl5           = (0xAD << 2);
constexpr uint32_t kHhiFixPllCntl6           = (0xAE << 2);
constexpr uint32_t kHhiFixPllSts             = (0xAF << 2);
constexpr uint32_t kHhiVdacCntl0             = (0xBB << 2);
constexpr uint32_t kHhiVdacCntl1             = (0xBC << 2);
constexpr uint32_t kHhiSysPllCntl0           = (0xBD << 2);
constexpr uint32_t kHhiSysPllCntl1           = (0xBE << 2);
constexpr uint32_t kHhiSysPllCntl2           = (0xBF << 2);
constexpr uint32_t kHhiSysPllCntl3           = (0xC0 << 2);
constexpr uint32_t kHhiSysPllCntl4           = (0xC1 << 2);
constexpr uint32_t kHhiSysPllCntl5           = (0xC2 << 2);
constexpr uint32_t kHhiSysPllCntl6           = (0xC3 << 2);
constexpr uint32_t kHhiSysPllSts             = (0xC4 << 2);
constexpr uint32_t kHhiHdmiPllCntl0          = (0xC8 << 2);
constexpr uint32_t kHhiHdmiPllCntl1          = (0xC9 << 2);
constexpr uint32_t kHhiHdmiPllCntl2          = (0xCA << 2);
constexpr uint32_t kHhiHdmiPllCntl3          = (0xCB << 2);
constexpr uint32_t kHhiHdmiPllCntl4          = (0xCC << 2);
constexpr uint32_t kHhiHdmiPllCntl5          = (0xCD << 2);
constexpr uint32_t kHhiHdmiPllCntl6          = (0xCE << 2);
constexpr uint32_t kHhiHdmiPllSts            = (0xCF << 2);
constexpr uint32_t kHhiHdmiPhyCntl0          = (0xE8 << 2);
constexpr uint32_t kHhiHdmiPhyCntl1          = (0xE9 << 2);
constexpr uint32_t kHhiHdmiPhyCntl2          = (0xEA << 2);
constexpr uint32_t kHhiHdmiPhyCntl3          = (0xEB << 2);
constexpr uint32_t kHhiHdmiPhyCntl4          = (0xEC << 2);
constexpr uint32_t kHhiHdmiPhyCntl5          = (0xED << 2);
constexpr uint32_t kHhiHdmiPhyStatus         = (0xEE << 2);
constexpr uint32_t kHhiVidLockClkCntl        = (0xF2 << 2);
constexpr uint32_t kHhiAxiPipelCntl          = (0xF4 << 2);
constexpr uint32_t kHhiBt656ClkCntl          = (0xF5 << 2);
constexpr uint32_t kHhiCdacClkCntl           = (0xF6 << 2);
constexpr uint32_t kHhiSpiccClkCntl          = (0xF7 << 2);
// clang-format on

// NOTE: This list only contains the clocks in use currently and
//       not all available clocks.
static constexpr meson_clk_gate_t g12a_clk_gates[] = {
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

    {.reg = kHhiGclk2Other, .bit = 1},   // CLK_VCLK2_VENCI0
    {.reg = kHhiGclk2Other, .bit = 2},   // CLK_VCLK2_VENCI1
    {.reg = kHhiGclk2Other, .bit = 3},   // CLK_VCLK2_VENCP0
    {.reg = kHhiGclk2Other, .bit = 4},   // CLK_VCLK2_VENCP1
    {.reg = kHhiGclk2Other, .bit = 5},   // CLK_VCLK2_VENCT0
    {.reg = kHhiGclk2Other, .bit = 6},   // CLK_VCLK2_VENCT1
    {.reg = kHhiGclk2Other, .bit = 7},   // CLK_VCLK2_OTHER
    {.reg = kHhiGclk2Other, .bit = 8},   // CLK_VCLK2_ENCI
    {.reg = kHhiGclk2Other, .bit = 9},   // CLK_VCLK2_ENCP
    {.reg = kHhiGclk2Other, .bit = 10},  // CLK_DAC_CLK
    {.reg = kHhiGclk2Other, .bit = 14},  // CLK_AOCLK_GATE
    {.reg = kHhiGclk2Other, .bit = 16},  // CLK_IEC958_GATE
    {.reg = kHhiGclk2Other, .bit = 20},  // CLK_ENC480P
    {.reg = kHhiGclk2Other, .bit = 21},  // CLK_RNG1
    {.reg = kHhiGclk2Other, .bit = 22},  // CLK_VCLK2_ENCT
    {.reg = kHhiGclk2Other, .bit = 23},  // CLK_VCLK2_ENCL
    {.reg = kHhiGclk2Other, .bit = 24},  // CLK_VCLK2_VENCLMMC
    {.reg = kHhiGclk2Other, .bit = 25},  // CLK_VCLK2_VENCL
    {.reg = kHhiGclk2Other, .bit = 26},  // CLK_VCLK2_OTHER1
};

static_assert(g12a_clk::CLK_G12A_COUNT == countof(g12a_clk_gates),
              "g12a_clk_gates[] and CLK_G12A_COUNT count mismatch");

static constexpr meson_clk_msr_t g12a_clk_msr = {
    .reg0_offset = (0x1 << 2),
    .reg2_offset = (0x3 << 2),
};

// This clock table is meant only for CLK-MEASURE
// Indexes here, correspond to actual clk mux
// values written to measure respective clk.
static const char* const g12a_clk_table[] = {
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
    "1'b0",                       // 91
    "1'b0",                       // 92
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
};
