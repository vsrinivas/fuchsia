// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <soc/aml-meson/aml-clk-common.h>

namespace sm1_clk {

// kMesonGate Clocks, common between sm1 and g12a.
constexpr uint32_t CLK_SYS_PLL_DIV16 = AmlClkId(0, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_SYS_CPU_CLK_DIV16 = AmlClkId(1, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_DDR = AmlClkId(2, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_DOS = AmlClkId(3, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_ALOCKER = AmlClkId(4, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_MIPI_DSI_HOST = AmlClkId(5, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_ETH_PHY = AmlClkId(6, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_ISA = AmlClkId(7, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_PL301 = AmlClkId(8, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_PERIPHS = AmlClkId(9, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_SPICC_0 = AmlClkId(10, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_I2C = AmlClkId(11, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_SANA = AmlClkId(12, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_SD = AmlClkId(13, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_RNG0 = AmlClkId(14, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_UART0 = AmlClkId(15, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_SPICC_1 = AmlClkId(16, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_HIU_REG = AmlClkId(17, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_MIPI_DSI_PHY = AmlClkId(18, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_ASSIST_MISC = AmlClkId(19, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_EMMC_A = AmlClkId(20, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_EMMC_B = AmlClkId(21, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_EMMC_C = AmlClkId(22, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_ACODEC = AmlClkId(23, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AUDIO = AmlClkId(24, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_ETH_CORE = AmlClkId(25, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_DEMUX = AmlClkId(26, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AIFIFO = AmlClkId(27, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_ADC = AmlClkId(28, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_UART1 = AmlClkId(29, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_G2D = AmlClkId(30, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_RESET = AmlClkId(31, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_PCIE_COMB = AmlClkId(32, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_PARSER = AmlClkId(33, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_USB_GENERAL = AmlClkId(34, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_PCIE_PHY = AmlClkId(35, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AHB_ARB0 = AmlClkId(36, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AHB_DATA_BUS = AmlClkId(37, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AHB_CTRL_BUS = AmlClkId(38, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_HTX_HDCP22 = AmlClkId(39, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_HTX_PCLK = AmlClkId(40, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_BT656 = AmlClkId(41, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_USB1_TO_DDR = AmlClkId(42, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_MMC_PCLK = AmlClkId(43, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_UART2 = AmlClkId(44, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_VPU_INTR = AmlClkId(45, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GIC = AmlClkId(46, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_VENCI0 = AmlClkId(47, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_VENCI1 = AmlClkId(48, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_VENCP0 = AmlClkId(49, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_VENCP1 = AmlClkId(50, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_VENCT0 = AmlClkId(51, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_VENCT1 = AmlClkId(52, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_OTHER = AmlClkId(53, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_ENCI = AmlClkId(54, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_ENCP = AmlClkId(55, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_DAC_CLK = AmlClkId(56, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AOCLK_GATE = AmlClkId(57, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_IEC958_GATE = AmlClkId(58, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_ENC480P = AmlClkId(59, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_RNG1 = AmlClkId(60, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_ENCT = AmlClkId(61, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_ENCL = AmlClkId(62, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_VENCLMMC = AmlClkId(63, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_VENCL = AmlClkId(64, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_OTHER1 = AmlClkId(65, aml_clk_common::aml_clk_type::kMesonGate);

// Clock gates specific to SM1.
constexpr uint32_t CLK_CSI_DIG = AmlClkId(66, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_NNA = AmlClkId(67, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_PARSER1 = AmlClkId(68, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_CSI_HOST = AmlClkId(69, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_CSI_ADPAT = AmlClkId(70, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_TEMP_SENSOR = AmlClkId(71, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_CSI_PHY = AmlClkId(72, aml_clk_common::aml_clk_type::kMesonGate);

constexpr uint32_t CLK_SM1_COUNT = 73;


// kMesonPllClocks
// constexpr uint32_t CLK_GP0_PLL  = AmlClkId(GP0_PLL,  aml_clk_common::aml_clk_type::kMesonPll);
// constexpr uint32_t CLK_PCIE_PLL = AmlClkId(PCIE_PLL, aml_clk_common::aml_clk_type::kMesonPll);
// constexpr uint32_t CLK_HIFI_PLL = AmlClkId(HIFI_PLL, aml_clk_common::aml_clk_type::kMesonPll);
// constexpr uint32_t CLK_SYS_PLL  = AmlClkId(SYS_PLL,  aml_clk_common::aml_clk_type::kMesonPll);
// constexpr uint32_t CLK_SYS1_PLL = AmlClkId(SYS1_PLL, aml_clk_common::aml_clk_type::kMesonPll);

}  // namespace sm1_clk
