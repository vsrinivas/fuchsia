// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_GXL_CLK_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_GXL_CLK_H_

#include <soc/aml-meson/aml-clk-common.h>

namespace gxl_clk {

// MPEG0
constexpr uint32_t CLK_GXL_DDR = AmlClkId(0, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_DOS = AmlClkId(1, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_ISA = AmlClkId(2, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_PL301 = AmlClkId(3, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_PERIPHS = AmlClkId(4, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_SPICC = AmlClkId(5, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_I2C = AmlClkId(6, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_SANA = AmlClkId(7, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_SMART_CARD = AmlClkId(8, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_RNG0 = AmlClkId(9, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_UART0 = AmlClkId(10, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_SDHC = AmlClkId(11, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_STREAM = AmlClkId(12, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_ASYNC_FIFO = AmlClkId(13, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_SDIO = AmlClkId(14, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_ABUF = AmlClkId(15, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_HIU_IFACE = AmlClkId(16, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_BT656 = AmlClkId(17, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_ASSIST_MISC = AmlClkId(18, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_EMMC_A = AmlClkId(19, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_EMMC_B = AmlClkId(20, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_EMMC_C = AmlClkId(21, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_DMA = AmlClkId(22, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_ACODEC = AmlClkId(23, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_SPI = AmlClkId(24, aml_clk_common::aml_clk_type::kMesonGate);

// MPEG1
constexpr uint32_t CLK_GXL_PCLK_TVFE = AmlClkId(25, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_I2S_SPDIF = AmlClkId(26, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_ETH = AmlClkId(27, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_DEMUX = AmlClkId(28, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_AIU_GLUE = AmlClkId(29, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_IEC958 = AmlClkId(30, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_I2S_OUT = AmlClkId(31, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_AMCLK = AmlClkId(32, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_AIFIFO2 = AmlClkId(33, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_MIXER = AmlClkId(34, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_MIXER_IFACE = AmlClkId(35, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_ADC = AmlClkId(36, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_BLKMV = AmlClkId(37, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_AIU_TOP = AmlClkId(38, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_UART1 = AmlClkId(39, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_G2D = AmlClkId(40, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_USB0 = AmlClkId(41, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_USB1 = AmlClkId(42, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_RESET = AmlClkId(43, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_NAND = AmlClkId(44, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_DOS_PARSER = AmlClkId(45, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_USB_GENERAL = AmlClkId(46, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_VDIN1 = AmlClkId(47, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_AHB_ARB0 = AmlClkId(48, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_EFUSE = AmlClkId(49, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_BOOT_ROM = AmlClkId(50, aml_clk_common::aml_clk_type::kMesonGate);

// MPEG2
constexpr uint32_t CLK_GXL_AHB_DATA_BUS = AmlClkId(51, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_AHB_CTRL_BUS = AmlClkId(52, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_HDCP22_PCLK = AmlClkId(53, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_HDMITX_PCLK = AmlClkId(54, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_PDM_PCLK = AmlClkId(55, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_BT656_PCLK = AmlClkId(56, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_USB1_TO_DDR = AmlClkId(57, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_USB0_TO_DDR = AmlClkId(58, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_AIU_PCLK = AmlClkId(59, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_MMC_PCLK = AmlClkId(60, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_DVIN = AmlClkId(61, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_UART2 = AmlClkId(62, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_SARADC = AmlClkId(63, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_VPU_INTR = AmlClkId(64, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_SEC_AHB_AHB3_BRIDGE =
    AmlClkId(65, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_APB3_AO = AmlClkId(66, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_MCLK_TVFE = AmlClkId(67, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_CLK81_GIC = AmlClkId(68, aml_clk_common::aml_clk_type::kMesonGate);

// Other
constexpr uint32_t CLK_GXL_VCLK2_VENCI0 = AmlClkId(69, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_VCLK2_VENCI1 = AmlClkId(70, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_VCLK2_VENCP0 = AmlClkId(71, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_VCLK2_VENCP1 = AmlClkId(72, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_VCLK2_VENCT0 = AmlClkId(73, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_VCLK2_VENCT1 = AmlClkId(74, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_VCLK2_OTHER = AmlClkId(75, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_VCLK2_ENCI = AmlClkId(76, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_VCLK2_ENCP = AmlClkId(77, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_DAC_CLK = AmlClkId(78, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_AOCLK_GATE = AmlClkId(79, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_IEC958_GATE = AmlClkId(80, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_ENC480P = AmlClkId(81, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_RNG1 = AmlClkId(82, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_VCLK2_ENCT = AmlClkId(83, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_VCLK2_ENCL = AmlClkId(84, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_VCLK2_VENCLMMC = AmlClkId(85, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_VCLK2_VENCL = AmlClkId(86, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_VCLK2_OTHER1 = AmlClkId(87, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t CLK_GXL_EDP = AmlClkId(88, aml_clk_common::aml_clk_type::kMesonGate);

// NB: This must be the last entry
constexpr uint32_t CLK_GXL_COUNT = 89;

}  // namespace gxl_clk

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_GXL_CLK_H_
