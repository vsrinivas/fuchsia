// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_A1_CLK_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_A1_CLK_H_

#include <stdint.h>

#include <soc/aml-meson/aml-clk-common.h>
#include <soc/aml-s905d2/s905d2-hiu.h>

namespace a1_clk {

using clk_type = ::aml_clk_common::aml_clk_type;

// kMesonGate Clocks
constexpr uint32_t CLK_DDS = AmlClkId(0, clk_type::kMesonGate);
constexpr uint32_t CLK_SYSPLL = AmlClkId(1, clk_type::kMesonGate);
constexpr uint32_t CLK_HIFIPLL = AmlClkId(2, clk_type::kMesonGate);
constexpr uint32_t CLK_USB_CTRL = AmlClkId(3, clk_type::kMesonGate);
constexpr uint32_t CLK_USB_PHY = AmlClkId(4, clk_type::kMesonGate);
constexpr uint32_t CLK_FIXPLL = AmlClkId(5, clk_type::kMesonGate);
constexpr uint32_t CLK_CLK_TREE = AmlClkId(6, clk_type::kMesonGate);
constexpr uint32_t CLK_RTC_IN = AmlClkId(7, clk_type::kMesonGate);
constexpr uint32_t CLK_RTC_OUT = AmlClkId(8, clk_type::kMesonGate);
constexpr uint32_t CLK_SYS_PRE_A = AmlClkId(9, clk_type::kMesonGate);
constexpr uint32_t CLK_SYS_PRE_B = AmlClkId(10, clk_type::kMesonGate);
constexpr uint32_t CLK_AXI_PRE_A = AmlClkId(11, clk_type::kMesonGate);
constexpr uint32_t CLK_AXI_PRE_B = AmlClkId(12, clk_type::kMesonGate);
constexpr uint32_t CLK_DSPA_PRE_A = AmlClkId(13, clk_type::kMesonGate);
constexpr uint32_t CLK_DSPA_PRE_B = AmlClkId(14, clk_type::kMesonGate);
constexpr uint32_t CLK_DSPB_PRE_A = AmlClkId(15, clk_type::kMesonGate);
constexpr uint32_t CLK_DSPB_PRE_B = AmlClkId(16, clk_type::kMesonGate);
constexpr uint32_t CLK_GEN = AmlClkId(17, clk_type::kMesonGate);
constexpr uint32_t CLK_TIMESTAMP = AmlClkId(18, clk_type::kMesonGate);
constexpr uint32_t CLK_ADC = AmlClkId(19, clk_type::kMesonGate);
constexpr uint32_t CLK_PWM_A = AmlClkId(20, clk_type::kMesonGate);
constexpr uint32_t CLK_PWM_B = AmlClkId(21, clk_type::kMesonGate);
constexpr uint32_t CLK_PWM_C = AmlClkId(22, clk_type::kMesonGate);
constexpr uint32_t CLK_PWM_D = AmlClkId(23, clk_type::kMesonGate);
constexpr uint32_t CLK_PWM_E = AmlClkId(24, clk_type::kMesonGate);
constexpr uint32_t CLK_PWM_F = AmlClkId(25, clk_type::kMesonGate);
constexpr uint32_t CLK_SPICC = AmlClkId(26, clk_type::kMesonGate);
constexpr uint32_t CLK_TS = AmlClkId(27, clk_type::kMesonGate);
constexpr uint32_t CLK_SPIFC = AmlClkId(28, clk_type::kMesonGate);
constexpr uint32_t CLK_USB_BUSCLK = AmlClkId(29, clk_type::kMesonGate);
constexpr uint32_t CLK_SD_EMMC = AmlClkId(30, clk_type::kMesonGate);
constexpr uint32_t CLK_CECA_IN = AmlClkId(31, clk_type::kMesonGate);
constexpr uint32_t CLK_CECA_OUT = AmlClkId(32, clk_type::kMesonGate);
constexpr uint32_t CLK_CECB_IN = AmlClkId(33, clk_type::kMesonGate);
constexpr uint32_t CLK_CECB_OUT = AmlClkId(34, clk_type::kMesonGate);
constexpr uint32_t CLK_PSRAM = AmlClkId(35, clk_type::kMesonGate);
constexpr uint32_t CLK_DMA = AmlClkId(36, clk_type::kMesonGate);
constexpr uint32_t CLK_A1_GATE_COUNT = 37;

// Muxes
constexpr uint32_t CLK_RTC_SEL = AmlClkId(0, clk_type::kMesonMux);
constexpr uint32_t CLK_SYS_PRE_A_SEL = AmlClkId(1, clk_type::kMesonMux);
constexpr uint32_t CLK_SYS_PRE_B_SEL = AmlClkId(2, clk_type::kMesonMux);
constexpr uint32_t CLK_AXI_PRE_A_SEL = AmlClkId(3, clk_type::kMesonMux);
constexpr uint32_t CLK_AXI_PRE_B_SEL = AmlClkId(4, clk_type::kMesonMux);
constexpr uint32_t CLK_DSPA_PRE_A_SEL = AmlClkId(5, clk_type::kMesonMux);
constexpr uint32_t CLK_DSPA_PRE_B_SEL = AmlClkId(6, clk_type::kMesonMux);
constexpr uint32_t CLK_DSPB_PRE_A_SEL = AmlClkId(7, clk_type::kMesonMux);
constexpr uint32_t CLK_DSPB_PRE_B_SEL = AmlClkId(8, clk_type::kMesonMux);
constexpr uint32_t CLK_GEN_SEL = AmlClkId(9, clk_type::kMesonMux);
constexpr uint32_t CLK_TIMESTAMP_SEL = AmlClkId(10, clk_type::kMesonMux);
constexpr uint32_t CLK_ADC_SEL = AmlClkId(11, clk_type::kMesonMux);
constexpr uint32_t CLK_PWM_A_SEL = AmlClkId(12, clk_type::kMesonMux);
constexpr uint32_t CLK_PWM_B_SEL = AmlClkId(13, clk_type::kMesonMux);
constexpr uint32_t CLK_PWM_C_SEL = AmlClkId(14, clk_type::kMesonMux);
constexpr uint32_t CLK_PWM_D_SEL = AmlClkId(15, clk_type::kMesonMux);
constexpr uint32_t CLK_PWM_E_SEL = AmlClkId(16, clk_type::kMesonMux);
constexpr uint32_t CLK_PWM_F_SEL = AmlClkId(17, clk_type::kMesonMux);
constexpr uint32_t CLK_SPICC_SEL = AmlClkId(18, clk_type::kMesonMux);
constexpr uint32_t CLK_SPIFC_SEL = AmlClkId(19, clk_type::kMesonMux);
constexpr uint32_t CLK_USB_BUSCLK_SEL = AmlClkId(20, clk_type::kMesonMux);
constexpr uint32_t CLK_SD_EMMC_SEL = AmlClkId(21, clk_type::kMesonMux);
constexpr uint32_t CLK_PSRAM_SEL = AmlClkId(22, clk_type::kMesonMux);
constexpr uint32_t CLK_DMC_SEL = AmlClkId(23, clk_type::kMesonMux);
constexpr uint32_t CLK_A1_MUX_COUNT = 24;

// kMesonPllClocks

// Cpu Clocks

}  // namespace a1_clk

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_A1_CLK_H_
