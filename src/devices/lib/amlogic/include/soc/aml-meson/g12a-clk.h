// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_G12A_CLK_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_G12A_CLK_H_

#include <soc/aml-meson/aml-clk-common.h>
#include <soc/aml-s905d2/s905d2-hiu.h>

namespace g12a_clk {

using clk_type = ::aml_clk_common::aml_clk_type;

// kMesonGate Clocks
constexpr uint32_t CLK_SYS_PLL_DIV16 = AmlClkId(0, clk_type::kMesonGate);
constexpr uint32_t CLK_SYS_CPU_CLK_DIV16 = AmlClkId(1, clk_type::kMesonGate);
constexpr uint32_t CLK_DDR = AmlClkId(2, clk_type::kMesonGate);
constexpr uint32_t CLK_DOS = AmlClkId(3, clk_type::kMesonGate);
constexpr uint32_t CLK_ALOCKER = AmlClkId(4, clk_type::kMesonGate);
constexpr uint32_t CLK_MIPI_DSI_HOST = AmlClkId(5, clk_type::kMesonGate);
constexpr uint32_t CLK_ETH_PHY = AmlClkId(6, clk_type::kMesonGate);
constexpr uint32_t CLK_ISA = AmlClkId(7, clk_type::kMesonGate);
constexpr uint32_t CLK_PL301 = AmlClkId(8, clk_type::kMesonGate);
constexpr uint32_t CLK_PERIPHS = AmlClkId(9, clk_type::kMesonGate);
constexpr uint32_t CLK_SPICC_0 = AmlClkId(10, clk_type::kMesonGate);
constexpr uint32_t CLK_I2C = AmlClkId(11, clk_type::kMesonGate);
constexpr uint32_t CLK_SANA = AmlClkId(12, clk_type::kMesonGate);
constexpr uint32_t CLK_SD = AmlClkId(13, clk_type::kMesonGate);
constexpr uint32_t CLK_RNG0 = AmlClkId(14, clk_type::kMesonGate);
constexpr uint32_t CLK_UART0 = AmlClkId(15, clk_type::kMesonGate);
constexpr uint32_t CLK_SPICC_1 = AmlClkId(16, clk_type::kMesonGate);
constexpr uint32_t CLK_HIU_REG = AmlClkId(17, clk_type::kMesonGate);
constexpr uint32_t CLK_MIPI_DSI_PHY = AmlClkId(18, clk_type::kMesonGate);
constexpr uint32_t CLK_ASSIST_MISC = AmlClkId(19, clk_type::kMesonGate);
constexpr uint32_t CLK_EMMC_A = AmlClkId(20, clk_type::kMesonGate);
constexpr uint32_t CLK_EMMC_B = AmlClkId(21, clk_type::kMesonGate);
constexpr uint32_t CLK_EMMC_C = AmlClkId(22, clk_type::kMesonGate);
constexpr uint32_t CLK_ACODEC = AmlClkId(23, clk_type::kMesonGate);
constexpr uint32_t CLK_AUDIO = AmlClkId(24, clk_type::kMesonGate);
constexpr uint32_t CLK_ETH_CORE = AmlClkId(25, clk_type::kMesonGate);
constexpr uint32_t CLK_DEMUX = AmlClkId(26, clk_type::kMesonGate);
constexpr uint32_t CLK_AIFIFO = AmlClkId(27, clk_type::kMesonGate);
constexpr uint32_t CLK_ADC = AmlClkId(28, clk_type::kMesonGate);
constexpr uint32_t CLK_UART1 = AmlClkId(29, clk_type::kMesonGate);
constexpr uint32_t CLK_G2D = AmlClkId(30, clk_type::kMesonGate);
constexpr uint32_t CLK_RESET = AmlClkId(31, clk_type::kMesonGate);
constexpr uint32_t CLK_PCIE_COMB = AmlClkId(32, clk_type::kMesonGate);
constexpr uint32_t CLK_PARSER = AmlClkId(33, clk_type::kMesonGate);
constexpr uint32_t CLK_USB_GENERAL = AmlClkId(34, clk_type::kMesonGate);
constexpr uint32_t CLK_PCIE_PHY = AmlClkId(35, clk_type::kMesonGate);
constexpr uint32_t CLK_AHB_ARB0 = AmlClkId(36, clk_type::kMesonGate);
constexpr uint32_t CLK_AHB_DATA_BUS = AmlClkId(37, clk_type::kMesonGate);
constexpr uint32_t CLK_AHB_CTRL_BUS = AmlClkId(38, clk_type::kMesonGate);
constexpr uint32_t CLK_HTX_HDCP22 = AmlClkId(39, clk_type::kMesonGate);
constexpr uint32_t CLK_HTX_PCLK = AmlClkId(40, clk_type::kMesonGate);
constexpr uint32_t CLK_BT656 = AmlClkId(41, clk_type::kMesonGate);
constexpr uint32_t CLK_USB1_TO_DDR = AmlClkId(42, clk_type::kMesonGate);
constexpr uint32_t CLK_MMC_PCLK = AmlClkId(43, clk_type::kMesonGate);
constexpr uint32_t CLK_UART2 = AmlClkId(44, clk_type::kMesonGate);
constexpr uint32_t CLK_VPU_INTR = AmlClkId(45, clk_type::kMesonGate);
constexpr uint32_t CLK_GIC = AmlClkId(46, clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_VENCI0 = AmlClkId(47, clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_VENCI1 = AmlClkId(48, clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_VENCP0 = AmlClkId(49, clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_VENCP1 = AmlClkId(50, clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_VENCT0 = AmlClkId(51, clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_VENCT1 = AmlClkId(52, clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_OTHER = AmlClkId(53, clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_ENCI = AmlClkId(54, clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_ENCP = AmlClkId(55, clk_type::kMesonGate);
constexpr uint32_t CLK_DAC_CLK = AmlClkId(56, clk_type::kMesonGate);
constexpr uint32_t CLK_AOCLK_GATE = AmlClkId(57, clk_type::kMesonGate);
constexpr uint32_t CLK_IEC958_GATE = AmlClkId(58, clk_type::kMesonGate);
constexpr uint32_t CLK_ENC480P = AmlClkId(59, clk_type::kMesonGate);
constexpr uint32_t CLK_RNG1 = AmlClkId(60, clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_ENCT = AmlClkId(61, clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_ENCL = AmlClkId(62, clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_VENCLMMC = AmlClkId(63, clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_VENCL = AmlClkId(64, clk_type::kMesonGate);
constexpr uint32_t CLK_VCLK2_OTHER1 = AmlClkId(65, clk_type::kMesonGate);
constexpr uint32_t CLK_DOS_GCLK_VDEC = AmlClkId(66, clk_type::kMesonGate);
constexpr uint32_t CLK_G12A_COUNT = 67;

// kMesonPllClocks
constexpr uint32_t CLK_GP0_PLL = AmlClkId(GP0_PLL, clk_type::kMesonPll);
constexpr uint32_t CLK_PCIE_PLL = AmlClkId(PCIE_PLL, clk_type::kMesonPll);
constexpr uint32_t CLK_HIFI_PLL = AmlClkId(HIFI_PLL, clk_type::kMesonPll);
constexpr uint32_t CLK_SYS_PLL = AmlClkId(SYS_PLL, clk_type::kMesonPll);
constexpr uint32_t CLK_SYS1_PLL = AmlClkId(SYS1_PLL, clk_type::kMesonPll);

}  // namespace g12a_clk

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_G12A_CLK_H_
