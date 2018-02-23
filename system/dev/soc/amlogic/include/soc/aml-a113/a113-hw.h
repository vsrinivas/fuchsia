// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define A113_GPIOX_START 0
#define A113_GPIOA_START 23
#define A113_GPIOB_START (A113_GPIOA_START + 21)
#define A113_GPIOY_START (A113_GPIOB_START + 15)
#define A113_GPIOZ_START (A113_GPIOY_START + 16)
#define A113_GPIOAO_START (A113_GPIOZ_START + 11)

#define A113_GPIOX(n) (A113_GPIOX_START + n)
#define A113_GPIOA(n) (A113_GPIOA_START + n)
#define A113_GPIOB(n) (A113_GPIOB_START + n)
#define A113_GPIOY(n) (A113_GPIOY_START + n)
#define A113_GPIOZ(n) (A113_GPIOZ_START + n)
#define A113_GPIOAO(n) (A113_GPIOAO_START + n)

#define GPIO_CTRL_OFFSET   0x0
#define GPIO_OUTPUT_OFFSET 0x1
#define GPIO_INPUT_OFFSET  0x2

#define GPIOAO_INPUT_OFFSET 0x1

#define GPIO_REG0_EN_N 0x0c
#define GPIO_REG1_EN_N 0x0f
#define GPIO_REG2_EN_N 0x12
#define GPIO_REG3_EN_N 0x15
#define GPIO_REG4_EN_N 0x18

#define PERIPHS_PIN_MUX_0 0x20
#define PERIPHS_PIN_MUX_1 0x21
#define PERIPHS_PIN_MUX_2 0x22
#define PERIPHS_PIN_MUX_3 0x23
#define PERIPHS_PIN_MUX_4 0x24
#define PERIPHS_PIN_MUX_5 0x25
#define PERIPHS_PIN_MUX_6 0x26
// NOTE: PERIPHS_PIN_MUX_7 is not specified by the manual
#define PERIPHS_PIN_MUX_8 0x28
#define PERIPHS_PIN_MUX_9 0x29
// NOTE: PERIPHS_PIN_MUX_A is not specified by the manual
#define PERIPHS_PIN_MUX_B 0x2b
#define PERIPHS_PIN_MUX_C 0x2c
#define PERIPHS_PIN_MUX_D 0x2d

// GPIO AO registers live in a seperate register bank.
#define AO_RTI_PIN_MUX_REG0 0x05
#define AO_RTI_PIN_MUX_REG1 0x06
#define AO_GPIO_O_EN_N      0x09

#define A113_PINMUX_ALT_FN_MAX 15

#define A113_TDM_PHYS_BASE 0xff642000

// USB MMIO and IRQ
#define DWC3_MMIO_BASE      0xff500000
#define DWC3_MMIO_LENGTH    0x100000
#define DWC3_IRQ            62
#define USB_PHY_IRQ         48

// Clock Control
#define AXG_HIU_BASE_PHYS 0xff63c000

#define AXG_HHI_PCIE_PLL_CNTL6   0x3C
#define AXG_HHI_GCLK_MPEG0       0x50
#define AXG_HHI_GCLK_MPEG1       0x51
#define AXG_HHI_GCLK_MPEG2       0x52
#define AXG_HHI_GCLK_AO          0x55
#define AXG_HHI_MPEG_CLK_CNTL    0x5D

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
