// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_CLOCK_DRIVERS_AMLOGIC_CLK_AML_A1_BLOCKS_H_
#define SRC_DEVICES_CLOCK_DRIVERS_AMLOGIC_CLK_AML_A1_BLOCKS_H_

#include <soc/aml-meson/a1-clk.h>

#include "aml-clk-blocks.h"

constexpr uint32_t kA1ClkctrlOscinCtrl = (0x0 << 2);
constexpr uint32_t kA1ClkctrlRtcByOscinCtrl0 = (0x1 << 2);
constexpr uint32_t kA1ClkctrlRtcCtrl = (0x3 << 2);
constexpr uint32_t kA1ClkctrlSysClkCtrl0 = (0x4 << 2);
constexpr uint32_t kA1ClkctrlAxiClkCtrl0 = (0x5 << 2);
constexpr uint32_t kA1ClkctrlSysClkEn0 = (0x7 << 2);
constexpr uint32_t kA1ClkctrlSycClkEn1 = (0x8 << 2);
constexpr uint32_t kA1ClkctrlAxiClkEn = (0x9 << 2);
constexpr uint32_t kA1ClkctrlDspaClkEn = (0xa << 2);
constexpr uint32_t kA1ClkctrlDspbClkEn = (0xb << 2);
constexpr uint32_t kA1ClkctrlDspaClkCtrl0 = (0xc << 2);
constexpr uint32_t kA1ClkctrlDspbClkCtrl0 = (0xd << 2);
constexpr uint32_t kA1ClkctrlGenClkCtrl = (0xf << 2);
constexpr uint32_t kA1ClkctrlTimestampCtrl0 = (0x10 << 2);
constexpr uint32_t kA1ClkctrlTimebaseCtrl0 = (0x15 << 2);
constexpr uint32_t kA1ClkctrlTimebaseCtrl1 = (0x16 << 2);
constexpr uint32_t kA1ClkctrlSarAdcClkCtrl = (0x30 << 2);
constexpr uint32_t kA1ClkctrlPwmClkABCtrl = (0x31 << 2);
constexpr uint32_t kA1ClkctrlPwmClkCDCtrl = (0x32 << 2);
constexpr uint32_t kA1ClkctrlPwmClkEFCtrl = (0x33 << 2);
constexpr uint32_t kA1ClkctrlSpiccClkCtrl = (0x34 << 2);
constexpr uint32_t kA1ClkctrlTsClkCtrl = (0x35 << 2);
constexpr uint32_t kA1ClkctrlSpifcClkCtrl = (0x36 << 2);
constexpr uint32_t kA1ClkctrlUsbBusclkCtrl = (0x37 << 2);
constexpr uint32_t kA1ClkctrlSdemmcClkCtrl = (0x38 << 2);
constexpr uint32_t kA1ClkctrlCecaClkCtrl0 = (0x39 << 2);
constexpr uint32_t kA1ClkctrlCecaClkCtrl1 = (0x3a << 2);
constexpr uint32_t kA1ClkctrlCecbClkCtrl0 = (0x3b << 2);
constexpr uint32_t kA1ClkctrlCecbClkCtrl1 = (0x3c << 2);
constexpr uint32_t kA1ClkctrlPsramClkCtrl0 = (0x3d << 2);
constexpr uint32_t kA1ClkctrlDmcClkCtrl1 = (0x3e << 2);

// ANA_CTRL

// clang-format off
static constexpr meson_clk_gate_t a1_clk_gates[] = {
    {.reg = kA1ClkctrlOscinCtrl, .bit = 6},         // CLK_DDS
    {.reg = kA1ClkctrlOscinCtrl, .bit = 5},         // CLK_SYSPLL
    {.reg = kA1ClkctrlOscinCtrl, .bit = 4},         // CLK_HIFIPLL
    {.reg = kA1ClkctrlOscinCtrl, .bit = 3},         // CLK_USB_CTRL
    {.reg = kA1ClkctrlOscinCtrl, .bit = 2},         // CLK_USB_PHY
    {.reg = kA1ClkctrlOscinCtrl, .bit = 1},         // CLK_FIXPLL
    {.reg = kA1ClkctrlOscinCtrl, .bit = 0},         // CLK_CLK_TREE
    {.reg = kA1ClkctrlRtcByOscinCtrl0, .bit = 31},  // CLK_RTC_IN
    {.reg = kA1ClkctrlRtcByOscinCtrl0, .bit = 30},  // CLK_RTC_OUT
    {.reg = kA1ClkctrlSysClkCtrl0, .bit = 13},      // CLK_SYS_PRE_A
    {.reg = kA1ClkctrlSysClkCtrl0, .bit = 29},      // CLK_SYS_RRE_B
    {.reg = kA1ClkctrlAxiClkCtrl0, .bit = 13},      // CLK_AXI_PRE_A
    {.reg = kA1ClkctrlAxiClkCtrl0, .bit = 29},      // CLK_AXI_PRE_B
    {.reg = kA1ClkctrlDspaClkCtrl0, .bit = 13},     // CLK_DSPA_PRE_A
    {.reg = kA1ClkctrlDspaClkCtrl0, .bit = 29},     // CLK_DSPA_PRE_B
    {.reg = kA1ClkctrlDspbClkCtrl0, .bit = 13},     // CLK_DSPB_PRE_A
    {.reg = kA1ClkctrlDspbClkCtrl0, .bit = 29},     // CLK_DSPB_PRE_B
    {.reg = kA1ClkctrlGenClkCtrl, .bit = 11},       // CLK_GEN
    {.reg = kA1ClkctrlTimestampCtrl0, .bit = 9},    // CLK_TIMESTAMP
    {.reg = kA1ClkctrlSarAdcClkCtrl, .bit = 8},     // CLK_ADC
    {.reg = kA1ClkctrlPwmClkABCtrl, .bit = 8},      // CLK_PWM_A
    {.reg = kA1ClkctrlPwmClkABCtrl, .bit = 24},     // CLK_PWM_B
    {.reg = kA1ClkctrlPwmClkCDCtrl, .bit = 8},      // CLK_PWM_C
    {.reg = kA1ClkctrlPwmClkCDCtrl, .bit = 24},     // CLK_PWM_D
    {.reg = kA1ClkctrlPwmClkEFCtrl, .bit = 8},      // CLK_PWM_E
    {.reg = kA1ClkctrlPwmClkEFCtrl, .bit = 24},     // CLK_PWM_F
    {.reg = kA1ClkctrlSpiccClkCtrl, .bit = 8},      // CLK_SPICC
    {.reg = kA1ClkctrlTsClkCtrl, .bit = 8},         // CLK_TS
    {.reg = kA1ClkctrlSpifcClkCtrl, .bit = 8},      // CLK_SPIFC
    {.reg = kA1ClkctrlUsbBusclkCtrl, .bit = 8},     // CLK_USB_BUSCLK
    {.reg = kA1ClkctrlSdemmcClkCtrl, .bit = 8},     // CLK_SD_EMMC
    {.reg = kA1ClkctrlCecaClkCtrl0, .bit = 31},     // CLK_CECA_IN
    {.reg = kA1ClkctrlCecaClkCtrl0, .bit = 30},     // CLK_CECA_OUT
    {.reg = kA1ClkctrlCecbClkCtrl0, .bit = 31},     // CLK_CECB_IN
    {.reg = kA1ClkctrlCecbClkCtrl0, .bit = 30},     // CLK_CECB_OUT
    {.reg = kA1ClkctrlPsramClkCtrl0, .bit = 8},     // CLK_PSRAM
    {.reg = kA1ClkctrlDmcClkCtrl1, .bit = 8},       // CLK_DMA
};

static_assert(a1_clk::CLK_A1_GATE_COUNT == std::size(a1_clk_gates),
              "a1_clk_gates[] and CLK_A1_COUNT count mismatch");

static constexpr meson_clk_mux_t a1_muxes[] = {
    {.reg = kA1ClkctrlRtcCtrl,
     .mask = 0x3,
     .shift = 0,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_RTC_SEL
    {.reg = kA1ClkctrlSysClkCtrl0,
     .mask = 0x7,
     .shift = 10,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_SYS_PRE_A_SEL
    {.reg = kA1ClkctrlSysClkCtrl0,
     .mask = 0x7,
     .shift = 26,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_SYS_PRE_B_SEL
    {.reg = kA1ClkctrlAxiClkCtrl0,
     .mask = 0x7,
     .shift = 10,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_AXI_PRE_A_SEL
    {.reg = kA1ClkctrlAxiClkCtrl0,
     .mask = 0x7,
     .shift = 26,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_AXI_PRE_B_SEL
    {.reg = kA1ClkctrlDspaClkCtrl0,
     .mask = 0x7,
     .shift = 10,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_DSPA_PRE_A_SEL
    {.reg = kA1ClkctrlDspaClkCtrl0,
     .mask = 0x7,
     .shift = 26,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_DSPA_PRE_B_SEL
    {.reg = kA1ClkctrlDspbClkCtrl0,
     .mask = 0x7,
     .shift = 10,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_DSPB_PRE_A_SEL
    {.reg = kA1ClkctrlDspbClkCtrl0,
     .mask = 0x7,
     .shift = 26,
     .n_inputs = 8,
     .inputs = nullptr},  // CLK_DSPB_PRE_B_SEL
    {.reg = kA1ClkctrlGenClkCtrl,
     .mask = 0xf,
     .shift = 12,
     .n_inputs = 16,
     .inputs = nullptr},  // CLK_GEN_SEL
    {.reg = kA1ClkctrlTimestampCtrl0,
     .mask = 0x3,
     .shift = 10,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_TIMESTAMP_SEL
    {.reg = kA1ClkctrlSarAdcClkCtrl,
     .mask = 0x3,
     .shift = 9,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_ADC_SEL
    {.reg = kA1ClkctrlPwmClkABCtrl,
     .mask = 0x3,
     .shift = 9,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_PWM_A_SEL
    {.reg = kA1ClkctrlPwmClkABCtrl,
     .mask = 0x3,
     .shift = 25,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_PWM_B_SEL
    {.reg = kA1ClkctrlPwmClkCDCtrl,
     .mask = 0x3,
     .shift = 9,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_PWM_C_SEL
    {.reg = kA1ClkctrlPwmClkCDCtrl,
     .mask = 0x3,
     .shift = 25,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_PWM_D_SEL
    {.reg = kA1ClkctrlPwmClkEFCtrl,
     .mask = 0x3,
     .shift = 9,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_PWM_E_SEL
    {.reg = kA1ClkctrlPwmClkEFCtrl,
     .mask = 0x3,
     .shift = 25,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_PWM_F_SEL
    {.reg = kA1ClkctrlSpiccClkCtrl,
     .mask = 0x3,
     .shift = 9,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_SPICC_SEL
    {.reg = kA1ClkctrlSpifcClkCtrl,
     .mask = 0x3,
     .shift = 9,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_SPIFC_SEL
    {.reg = kA1ClkctrlUsbBusclkCtrl,
     .mask = 0x3,
     .shift = 9,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_USB_BUSCLK_SEL
    {.reg = kA1ClkctrlSdemmcClkCtrl,
     .mask = 0x3,
     .shift = 9,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_SD_EMMC_SEL
    {.reg = kA1ClkctrlPsramClkCtrl0,
     .mask = 0x3,
     .shift = 9,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_PSRAM_SEL
    {.reg = kA1ClkctrlDmcClkCtrl1,
     .mask = 0x3,
     .shift = 9,
     .n_inputs = 4,
     .inputs = nullptr},  // CLK_DMC_SEL
};

static_assert(a1_clk::CLK_A1_MUX_COUNT == std::size(a1_muxes),
              "a1_muxes and CLK_A1_MUX_COUNT count mismatch");

static constexpr meson_clk_msr_t a1_clk_msr = {
    .reg0_offset = (0x0 << 2),
    .reg2_offset = (0x2 << 2),
};

// Here the index id is the clock measurement id,
// so we need to add the "__reserved__" field to skip
// some useless ids.
static const char* const a1_clk_table[] = {
    "tdmout_b_sclk",               // 0
    "tdmout_a_sclk",               // 1
    "tdmin_lb_sclk",               // 2
    "tdmin_b_sclk",                // 3
    "tdmin_a_sclk",                // 4
    "vad_clk",                     // 5
    "resampleA_clk",               // 6
    "pdm_sysclk",                  // 7
    "pdm_dclk",                    // 8
    "locker_out_clk",              // 9
    "locker_in_clk",               // 10
    "spdifin_clk",                 // 11
    "tdmin_vad_sclk",              // 12
    "au_adc_clk",                  // 13
    "au_dac_clk",                  // 14
    "__reserved__",                // 15
    "cts_spicc_a_clk",             // 16
    "cts_spifc_clk",               // 17
    "cts_sd_emmc_a_clk",           // 18
    "cts_dmcx4_clk",               // 19
    "cts_dmc_clk",                 // 20
    "cts_psram_clk",               // 21
    "cts_cecb_clk",                // 22
    "cts_ceca_clk",                // 23
    "cts_ts_clk",                  // 24
    "cts_pwm_f_clk",               // 25
    "cts_pwm_e_clk",               // 26
    "cts_pwm_d_clk",               // 27
    "cts_pwm_c_clk",               // 28
    "cts_pwm_b_clk",               // 29
    "cts_pwm_a_clk",               // 30
    "cts_sar_adc_clk",             // 31
    "cts_usb_busclk",              // 32
    "clk_dspb",                    // 33
    "clk_dspa",                    // 34
    "clk_axi",                     // 35
    "clk_sys",                     // 36
    "__reserved__",                // 37
    "__reserved__",                // 38
    "__reserved__",                // 39
    "rng_ring_osc0",               // 40
    "rng_ring_osc1",               // 41
    "rng_ring_osc2",               // 42
    "rng_ring_osc3",               // 43
    "dds_out",                     // 44
    "cpu_clk_div16",               // 45
    "gpio_msr",                    // 46
    "__reserved__",                // 47
    "__reserved__",                // 48
    "__reserved__",                // 49
    "osc_ring_cpu0",               // 50
    "osc_ring_cpu1",               // 51
    "__reserved__",                // 52
    "__reserved__",                // 53
    "osc_ring_top0",               // 54
    "osc_ring_top1",               // 55
    "osc_ring_ddr",                // 56
    "osc_ring_dmc",                // 57
    "osc_ring_dspa",               // 58
    "osc_ring_dspb",               // 59
    "osc_ring_rama",               // 60
    "osc_ring_ramb",               // 61
};
// clang-format on

#endif  // SRC_DEVICES_CLOCK_DRIVERS_AMLOGIC_CLK_AML_A1_BLOCKS_H_
