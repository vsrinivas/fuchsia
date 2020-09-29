// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_AXG_CLK_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_AXG_CLK_H_

#include <soc/aml-meson/aml-clk-common.h>

namespace axg_clk {

// MPEG0 Reg Clocks
constexpr uint32_t CLK_AXG_DDR = AmlClkId(0, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_AUDIO_LOCKER = AmlClkId(1, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_MIPI_DSI_HOST = AmlClkId(2, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_ISA = AmlClkId(3, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_PL301 = AmlClkId(4, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_PERIPHS = AmlClkId(5, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_SPICC_0 = AmlClkId(6, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_I2C = AmlClkId(7, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_RNG0 = AmlClkId(8, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_UART0 = AmlClkId(9, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_MIPI_DSI_PHY = AmlClkId(10, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_SPICC_1 = AmlClkId(11, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_PCIE_A = AmlClkId(12, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_PCIE_B = AmlClkId(13, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_HIU_REG = AmlClkId(14, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_ASSIST_MISC = AmlClkId(15, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_EMMC_B = AmlClkId(16, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_EMMC_C = AmlClkId(17, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_DMA = AmlClkId(18, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_SPI = AmlClkId(19, aml_clk_common::aml_clk_type::kMesonGate);

// MPEG1 Reg Clocks
constexpr uint32_t CLK_AXG_AUDIO = AmlClkId(20, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_ETH_CORE = AmlClkId(21, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_UART1 = AmlClkId(22, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_G2D = AmlClkId(23, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_USB0 = AmlClkId(24, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_USB1 = AmlClkId(25, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_RESET = AmlClkId(26, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_USB_GENERAL = AmlClkId(27, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_AHB_ARB0 = AmlClkId(28, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_EFUSE = AmlClkId(29, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_BOOT_ROM = AmlClkId(30, aml_clk_common::aml_clk_type::kMesonGate);

// MPEG2 Reg Clocks
constexpr uint32_t CLK_AXG_AHB_DATA_BUS = AmlClkId(31, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_AHB_CTRL_BUS = AmlClkId(32, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_USB1_TO_DDR = AmlClkId(33, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_USB0_TO_DDR = AmlClkId(34, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_MMC_PCLK = AmlClkId(35, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_VPU_INTR = AmlClkId(36, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_SEC_AHB_AHB3_BRIDGE =
    AmlClkId(37, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_GIC = AmlClkId(38, aml_clk_common::aml_clk_type::kMesonGate);

// AO Domain Clocks
constexpr uint32_t CLK_AXG_AO_MEDIA_CPU = AmlClkId(39, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_AO_AHB_SRAM = AmlClkId(40, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_AO_AHB_BUS = AmlClkId(41, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_AO_IFACE = AmlClkId(42, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_AO_I2C = AmlClkId(43, aml_clk_common::aml_clk_type::kMesonGate);

// Etc...
constexpr uint32_t CLK_AXG_CLK81 = AmlClkId(44, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_CML0_EN = AmlClkId(45, aml_clk_common::aml_clk_type::kMesonGate);

// Dos clocks.
constexpr uint32_t CLK_DOS_GCLK_VDEC = AmlClkId(46, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_AXG_DOS = AmlClkId(47, aml_clk_common::aml_clk_type::kMesonGate);

// NB: This must be the last entry
constexpr uint32_t CLK_AXG_COUNT = 48;

}  // namespace axg_clk

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_AXG_CLK_H_
