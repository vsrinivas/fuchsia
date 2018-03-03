// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

typedef enum axg_clk_gate_idx {
    // MPEG0 Reg Clocks
    CLK_AXG_DDR = 0,
    CLK_AXG_AUDIO_LOCKER,
    CLK_AXG_MIPI_DSI_HOST,
    CLK_AXG_ISA,
    CLK_AXG_PL301,
    CLK_AXG_PERIPHS,
    CLK_AXG_SPICC_0,
    CLK_AXG_I2C,
    CLK_AXG_RNG0,
    CLK_AXG_UART0,
    CLK_AXG_MIPI_DSI_PHY,
    CLK_AXG_SPICC_1,
    CLK_AXG_PCIE_A,
    CLK_AXG_PCIE_B,
    CLK_AXG_HIU_REG,
    CLK_AXG_ASSIST_MISC,
    CLK_AXG_EMMC_B,
    CLK_AXG_EMMC_C,
    CLK_AXG_DMA,
    CLK_AXG_SPI,

    // MPEG1 Reg Clocks
    CLK_AXG_AUDIO,
    CLK_AXG_ETH_CORE,
    CLK_AXG_UART1,
    CLK_AXG_G2D,
    CLK_AXG_USB0,
    CLK_AXG_USB1,
    CLK_AXG_RESET,
    CLK_AXG_USB_GENERAL,
    CLK_AXG_AHB_ARB0,
    CLK_AXG_EFUSE,
    CLK_AXG_BOOT_ROM,

    // MPEG2 Reg Clocks
    CLK_AXG_AHB_DATA_BUS,
    CLK_AXG_AHB_CTRL_BUS,
    CLK_AXG_USB1_TO_DDR,
    CLK_AXG_USB0_TO_DDR,
    CLK_AXG_MMC_PCLK,
    CLK_AXG_VPU_INTR,
    CLK_AXG_SEC_AHB_AHB3_BRIDGE,
    CLK_AXG_GIC,

    // AO Domain Clocks
    CLK_AXG_AO_MEDIA_CPU,
    CLK_AXG_AO_AHB_SRAM,
    CLK_AXG_AO_AHB_BUS,
    CLK_AXG_AO_IFACE,
    CLK_AXG_AO_I2C,

    // Etc...
    CLK_AXG_CLK81,
    CLK_CML0_EN,

    // NB: This must be the last entry
    CLK_AXG_COUNT,
} axg_clk_gate_idx_t;
