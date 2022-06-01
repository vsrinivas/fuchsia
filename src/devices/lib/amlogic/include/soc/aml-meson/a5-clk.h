// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_A5_CLK_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_A5_CLK_H_

#include <stdint.h>

#include <soc/aml-meson/aml-clk-common.h>
#include <soc/aml-s905d2/s905d2-hiu.h>

namespace a5_clk {

using clk_type = ::aml_clk_common::aml_clk_type;

// kMesonGate Clocks
constexpr uint32_t CLK_USB_CTRL = AmlClkId(0, clk_type::kMesonGate);
constexpr uint32_t CLK_USB_PLL = AmlClkId(1, clk_type::kMesonGate);
constexpr uint32_t CLK_PLL_TOP = AmlClkId(2, clk_type::kMesonGate);
constexpr uint32_t CLK_DDR_PHY = AmlClkId(3, clk_type::kMesonGate);
constexpr uint32_t CLK_DDR_PLL = AmlClkId(4, clk_type::kMesonGate);
constexpr uint32_t CLK_RTC_IN = AmlClkId(5, clk_type::kMesonGate);
constexpr uint32_t CLK_RTC_OUT = AmlClkId(6, clk_type::kMesonGate);
constexpr uint32_t CLK_SYS_PRE_A = AmlClkId(7, clk_type::kMesonGate);
constexpr uint32_t CLK_SYS_RRE_B = AmlClkId(8, clk_type::kMesonGate);
constexpr uint32_t CLK_AXI_PRE_A = AmlClkId(9, clk_type::kMesonGate);
constexpr uint32_t CLK_AXI_PRE_B = AmlClkId(10, clk_type::kMesonGate);
constexpr uint32_t CLK_RAMA_PRE_A = AmlClkId(11, clk_type::kMesonGate);
constexpr uint32_t CLK_RAMA_PRE_B = AmlClkId(12, clk_type::kMesonGate);
constexpr uint32_t CLK_DSPA_PRE_A = AmlClkId(13, clk_type::kMesonGate);
constexpr uint32_t CLK_DSPA_PRE_B = AmlClkId(14, clk_type::kMesonGate);
constexpr uint32_t CLK_CLK25 = AmlClkId(15, clk_type::kMesonGate);
constexpr uint32_t CLK_CLK24 = AmlClkId(16, clk_type::kMesonGate);
constexpr uint32_t CLK_CLK24_DIV2 = AmlClkId(17, clk_type::kMesonGate);
constexpr uint32_t CLK_ETH_RMII = AmlClkId(18, clk_type::kMesonGate);
constexpr uint32_t CLK_ETH_125M = AmlClkId(19, clk_type::kMesonGate);
constexpr uint32_t CLK_TS = AmlClkId(20, clk_type::kMesonGate);
constexpr uint32_t CLK_NAND = AmlClkId(21, clk_type::kMesonGate);
constexpr uint32_t CLK_SD_EMMC_A = AmlClkId(22, clk_type::kMesonGate);
constexpr uint32_t CLK_SPICC_0 = AmlClkId(23, clk_type::kMesonGate);
constexpr uint32_t CLK_SPICC_1 = AmlClkId(24, clk_type::kMesonGate);
constexpr uint32_t CLK_PWM_A = AmlClkId(25, clk_type::kMesonGate);
constexpr uint32_t CLK_PWM_B = AmlClkId(26, clk_type::kMesonGate);
constexpr uint32_t CLK_PWM_C = AmlClkId(27, clk_type::kMesonGate);
constexpr uint32_t CLK_PWM_D = AmlClkId(28, clk_type::kMesonGate);
constexpr uint32_t CLK_PWM_E = AmlClkId(29, clk_type::kMesonGate);
constexpr uint32_t CLK_PWM_F = AmlClkId(30, clk_type::kMesonGate);
constexpr uint32_t CLK_PWM_G = AmlClkId(31, clk_type::kMesonGate);
constexpr uint32_t CLK_PWM_H = AmlClkId(32, clk_type::kMesonGate);
constexpr uint32_t CLK_ADC = AmlClkId(33, clk_type::kMesonGate);
constexpr uint32_t CLK_GEN = AmlClkId(34, clk_type::kMesonGate);
constexpr uint32_t CLK_NNA_AXI = AmlClkId(35, clk_type::kMesonGate);
constexpr uint32_t CLK_NNA_CORE = AmlClkId(36, clk_type::kMesonGate);
constexpr uint32_t CLK_TIMESTAMP = AmlClkId(37, clk_type::kMesonGate);
constexpr uint32_t CLK_A5_GATE_COUNT = 38;

// Muxes
constexpr uint32_t CLK_OSC_SEL = AmlClkId(0, clk_type::kMesonMux);
constexpr uint32_t CLK_RTC_SEL = AmlClkId(1, clk_type::kMesonMux);
constexpr uint32_t CLK_SYS_PRE_A_SEL = AmlClkId(2, clk_type::kMesonMux);
constexpr uint32_t CLK_SYS_PRE_B_SEL = AmlClkId(3, clk_type::kMesonMux);
constexpr uint32_t CLK_AXI_PRE_A_SEL = AmlClkId(4, clk_type::kMesonMux);
constexpr uint32_t CLK_AXI_PRE_B_SEL = AmlClkId(5, clk_type::kMesonMux);
constexpr uint32_t CLK_RAMA_PRE_A_SEL = AmlClkId(6, clk_type::kMesonMux);
constexpr uint32_t CLK_RAMA_PRE_B_SEL = AmlClkId(7, clk_type::kMesonMux);
constexpr uint32_t CLK_DSPA_PRE_A_SEL = AmlClkId(8, clk_type::kMesonMux);
constexpr uint32_t CLK_DSPA_PRE_B_SEL = AmlClkId(9, clk_type::kMesonMux);
constexpr uint32_t CLK_ETH_RMII_SEL = AmlClkId(10, clk_type::kMesonMuxRo);
constexpr uint32_t CLK_NAND_SEL = AmlClkId(11, clk_type::kMesonMuxRo);
constexpr uint32_t CLK_SD_EMMCA_SEL = AmlClkId(12, clk_type::kMesonMuxRo);
constexpr uint32_t CLK_SPICC0_SEL = AmlClkId(13, clk_type::kMesonMuxRo);
constexpr uint32_t CLK_SPICC1_SEL = AmlClkId(14, clk_type::kMesonMuxRo);
constexpr uint32_t CLK_PWM_A_SEL = AmlClkId(15, clk_type::kMesonMuxRo);
constexpr uint32_t CLK_PWM_B_SEL = AmlClkId(16, clk_type::kMesonMuxRo);
constexpr uint32_t CLK_PWM_C_SEL = AmlClkId(17, clk_type::kMesonMuxRo);
constexpr uint32_t CLK_PWM_D_SEL = AmlClkId(18, clk_type::kMesonMuxRo);
constexpr uint32_t CLK_PWM_E_SEL = AmlClkId(19, clk_type::kMesonMuxRo);
constexpr uint32_t CLK_PWM_F_SEL = AmlClkId(20, clk_type::kMesonMuxRo);
constexpr uint32_t CLK_PWM_G_SEL = AmlClkId(21, clk_type::kMesonMuxRo);
constexpr uint32_t CLK_PWM_H_SEL = AmlClkId(22, clk_type::kMesonMuxRo);
constexpr uint32_t CLK_ADC_SEL = AmlClkId(23, clk_type::kMesonMuxRo);
constexpr uint32_t CLK_GEN_SEL = AmlClkId(24, clk_type::kMesonMuxRo);
constexpr uint32_t CLK_NNA_AXI_SEL = AmlClkId(25, clk_type::kMesonMuxRo);
constexpr uint32_t CLK_NNA_CORE_SEL = AmlClkId(26, clk_type::kMesonMuxRo);
constexpr uint32_t CLK_TIMESTAMP_SEL = AmlClkId(27, clk_type::kMesonMuxRo);
constexpr uint32_t CLK_A5_MUX_COUNT = 28;

// kMesonPllClocks
constexpr uint32_t CLK_GP0_PLL = AmlClkId(GP0_PLL, clk_type::kMesonPll);
constexpr uint32_t CLK_HIFI_PLL = AmlClkId(HIFI_PLL, clk_type::kMesonPll);
constexpr uint32_t CLK_SYS_PLL = AmlClkId(SYS_PLL, clk_type::kMesonPll);

}  // namespace a5_clk

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_A5_CLK_H_
