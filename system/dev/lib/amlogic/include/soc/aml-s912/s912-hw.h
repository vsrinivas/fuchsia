// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Display Related Registers
#define S912_PRESET_BASE                0xC1104000
#define S912_PRESET_LENGTH              0x1000
#define S912_CBUS_REG_BASE              0xC8834000
#define S912_CBUS_REG_LENGTH            0x1000
#define S912_DMC_REG_BASE               0xC8838000
#define S912_DMC_REG_LENGTH             0x1000
#define S912_HDMITX_BASE                0xC883A000
#define S912_HDMITX_LENGTH              0x2000
#define S912_HIU_BASE                   0xC883C000
#define S912_HIU_LENGTH                 0x2000
#define S912_MALI_BASE                  0xD00C0000
#define S912_MALI_LENGTH                0x40000
#define S912_VPU_BASE                   0xD0100000
#define S912_VPU_LENGTH                 0x40000
#define S912_HDMITX_SEC_BASE            0xDA83A000
#define S912_HDMITX_SEC_LENGTH          0x2000

#define S912_AOBUS_BASE                 0xc8100000
#define S912_AOBUS_LENGTH               0x100000

#define S912_GPIO_BASE                  0xc8834400
#define S912_GPIO_LENGTH                0x1C00
#define S912_GPIO_A0_BASE               0xc8100000
#define S912_GPIO_AO_LENGTH             0x1000
#define S912_GPIO_INTERRUPT_BASE        0xC1100000
#define S912_GPIO_INTERRUPT_LENGTH      0x10000

#define S912_I2C_A_BASE                 0xc1108500
#define S912_I2C_A_LENGTH               0x20
#define S912_I2C_B_BASE                 0xc11087c0
#define S912_I2C_B_LENGTH               0x20
#define S912_I2C_C_BASE                 0xc11087e0
#define S912_I2C_C_LENGTH               0x20
#define S912_I2C_D_BASE                 0xc1108d20
#define S912_I2C_D_LENGTH               0x20

#define S912_USB0_BASE                  0xc9000000
#define S912_USB0_LENGTH                0x100000
#define S912_USB1_BASE                  0xc9100000
#define S912_USB1_LENGTH                0x100000

#define S912_USB_PHY_BASE               0xd0078000
#define S912_USB_PHY_LENGTH             0x1000

#define S912_UART_A_BASE                0xc11084c0
#define S912_UART_A_LENGTH              0x18
#define S912_UART_AO_B_BASE             0xc81004e0
#define S912_UART_AO_B_LENGTH           0x18

#define S912_DOS_BASE                   0xc8820000
#define S912_DOS_LENGTH                 0x10000

#define S912_HIU_MAILBOX_BASE           0xc883C400
#define S912_HIU_MAILBOX_LENGTH         0x1000

#define S912_MAILBOX_PAYLOAD_BASE       0xc8013000
#define S912_MAILBOX_PAYLOAD_LENGTH     0x1000

#define S912_FULL_CBUS_BASE              0xC1100000
#define S912_FULL_CBUS_LENGTH            0x100000

// IRQs
#define S912_VIU1_VSYNC_IRQ             35
#define S912_M_I2C_0_IRQ                53
#define S912_DEMUX_IRQ                  55
#define S912_UART_A_IRQ                 58
#define S912_USBH_IRQ                   62
#define S912_USBD_IRQ                   63
#define S912_PARSER_IRQ                 64
#define S912_M_I2C_3_IRQ                71
#define S912_DOS_MBOX_0_IRQ             75
#define S912_DOS_MBOX_1_IRQ             76
#define S912_DOS_MBOX_2_IRQ             77
#define S912_GPIO_IRQ_0                 96
#define S912_GPIO_IRQ_1                 97
#define S912_GPIO_IRQ_2                 98
#define S912_GPIO_IRQ_3                 99
#define S912_GPIO_IRQ_4                 100
#define S912_GPIO_IRQ_5                 101
#define S912_GPIO_IRQ_6                 102
#define S912_GPIO_IRQ_7                 103
#define S912_MALI_IRQ_GP                192
#define S912_MALI_IRQ_GPMMU             193
#define S912_MALI_IRQ_PP                194
#define S912_UART_AO_B_IRQ              229
#define S912_A0_GPIO_IRQ_0              232
#define S912_A0_GPIO_IRQ_1              233
#define S912_MBOX_IRQ_RECEIV0           240
#define S912_MBOX_IRQ_RECEIV1           241
#define S912_MBOX_IRQ_RECEIV2           242
#define S912_MBOX_IRQ_SEND3             243
#define S912_MBOX_IRQ_SEND4             244
#define S912_MBOX_IRQ_SEND5             245
#define S912_M_I2C_1_IRQ                246
#define S912_M_I2C_2_IRQ                247

// DMC registers
#define DMC_REG_BASE        0xc8838000

#define PERIPHS_REG_BASE        (0xc8834000)
#define PERIPHS_REG_SIZE        (0x2000)
//Offsets of peripheral control registers
#define PER_ETH_REG0            (0x400 + (0x50 << 2))
#define PER_ETH_REG1            (0x400 + (0x51 << 2))
#define PER_ETH_REG2            (0x400 + (0x56 << 2))
#define PER_ETH_REG3            (0x400 + (0x57 << 2))
#define PER_ETH_REG4            (0x400 + (0x58 << 2))


#define HHI_REG_BASE            (0xc883c000)
#define HHI_REG_SIZE            (0x2000)
//Offsets of HHI registers
#define HHI_SCR                 (0x0B << 2)
#define HHI_TIMEOUT_VALUE       (0x0F << 2)
#define HHI_GP0_PLL_CNTL        (0x10 << 2)
#define HHI_GP0_PLL_CNTL2       (0x11 << 2)
#define HHI_GP0_PLL_CNTL3       (0x12 << 2)
#define HHI_GP0_PLL_CNTL4       (0x13 << 2)
#define HHI_GP0_PLL_CNTL5       (0x14 << 2)
#define HHI_GP0_PLL_STS         (0x15 << 2)
#define HHI_GP0_PLL_CNTL1       (0x16 << 2)
#define HHI_XTAL_DIVN_CNTL      (0x2F << 2)
#define HHI_GCLK2_MPEG0         (0x30 << 2)
#define HHI_GCLK2_MPEG1         (0x31 << 2)
#define HHI_GCLK2_MPEG2         (0x32 << 2)
#define HHI_GCLK2_OTHER         (0x34 << 2)
#define HHI_GCLK2_AO            (0x35 << 2)
#define HHI_TIMER90K            (0x3B << 2)
#define HHI_MEM_PD_REG0         (0x40 << 2)
#define HHI_VPU_MEM_PD_REG0     (0x41 << 2)
#define HHI_VPU_MEM_PD_REG1     (0x42 << 2)
#define HHI_VIID_CLK_DIV        (0x4A << 2)
#define HHI_VIID_CLK_CNTL       (0x4B << 2)
#define HHI_GCLK_MPEG0          (0x50 << 2)
#define HHI_GCLK_MPEG1          (0x51 << 2)
#define HHI_GCLK_MPEG2          (0x52 << 2)
#define HHI_GCLK_OTHER          (0x54 << 2)
#define HHI_GCLK_AO             (0x55 << 2)
#define HHI_SYS_CPU_CLK_CNTL1   (0x57 << 2)
#define HHI_SYS_CPU_RESET_CNTL  (0x58 << 2)
#define HHI_VID_CLK_DIV         (0x59 << 2)
#define HHI_MPEG_CLK_CNTL       (0x5D << 2)
#define HHI_AUD_CLK_CNTL        (0x5E << 2)
#define HHI_VID_CLK_CNTL        (0x5F << 2)
#define HHI_AUD_CLK_CNTL2       (0x64 << 2)
#define HHI_VID_CLK_CNTL2       (0x65 << 2)
#define HHI_SYS_CPU_CLK_CNTL0   (0x67 << 2)
#define HHI_VID_PLL_CLK_DIV     (0x68 << 2)
#define HHI_AUD_CLK_CNTL3       (0x69 << 2)
#define HHI_MALI_CLK_CNTL       (0x6C << 2)
#define HHI_VPU_CLK_CNTL        (0x6F << 2)
#define HHI_HDMI_CLK_CNTL       (0x73 << 2)
#define HHI_VDEC_CLK_CNTL       (0x78 << 2)
#define HHI_VDEC2_CLK_CNTL      (0x79 << 2)
#define HHI_VDEC3_CLK_CNTL      (0x7a << 2)
#define HHI_VDEC4_CLK_CNTL      (0x7b << 2)
#define HHI_HDCP22_CLK_CNTL     (0x7c << 2)
#define HHI_VAPBCLK_CNTL        (0x7d << 2)
#define HHI_VPU_CLKB_CNTL       (0x83 << 2)
#define HHI_USB_CLK_CNTL        (0x88 << 2)
#define HHI_32K_CLK_CNTL        (0x89 << 2)
#define HHI_GEN_CLK_CNTL        (0x8A << 2)
#define HHI_PCM_CLK_CNTL        (0x96 << 2)
#define HHI_NAND_CLK_CNTL       (0x97 << 2)
#define HHI_SD_EMMC_CLK_CNTL    (0x99 << 2)
#define HHI_MPLL_CNTL           (0xA0 << 2)
#define HHI_MPLL_CNTL2          (0xA1 << 2)
#define HHI_MPLL_CNTL3          (0xA2 << 2)
#define HHI_MPLL_CNTL4          (0xA3 << 2)
#define HHI_MPLL_CNTL5          (0xA4 << 2)
#define HHI_MPLL_CNTL6          (0xA5 << 2)
#define HHI_MPLL_CNTL7          (0xA6 << 2)
#define HHI_MPLL_CNTL8          (0xA7 << 2)
#define HHI_MPLL_CNTL9          (0xA8 << 2)
#define HHI_MPLL_CNTL10         (0xA9 << 2)
#define HHI_MPLL3_CNTL0         (0xB8 << 2)
#define HHI_MPLL3_CNTL1         (0xB9 << 2)
#define HHI_VDAC_CNTL0          (0xBD << 2)
#define HHI_VDAC_CNTL1          (0xBE << 2)
#define HHI_SYS_PLL_CNTL1       (0xBF << 2)
#define HHI_SYS_PLL_CNTL        (0xC0 << 2)
#define HHI_SYS_PLL_CNTL2       (0xC1 << 2)
#define HHI_SYS_PLL_CNTL3       (0xC2 << 2)
#define HHI_SYS_PLL_CNTL4       (0xC3 << 2)
#define HHI_SYS_PLL_CNTL5       (0xC4 << 2)
#define HHI_SYS_PLL_STS         (0xC5 << 2)
#define HHI_DPLL_TOP_I          (0xC6 << 2)
#define HHI_DPLL_TOP2_I         (0xC7 << 2)
#define HHI_HDMI_PLL_CNTL       (0xC8 << 2)
#define HHI_HDMI_PLL_CNTL1      (0xC9 << 2)
#define HHI_HDMI_PLL_CNTL2      (0xCA << 2)
#define HHI_HDMI_PLL_CNTL3      (0xCB << 2)
#define HHI_HDMI_PLL_CNTL4      (0xCC << 2)
#define HHI_HDMI_PLL_CNTL5      (0xCD << 2)
#define HHI_HDMI_PLL_STS        (0xCE << 2)
#define HHI_HDMI_PHY_CNTL0      (0xE8 << 2)
#define HHI_HDMI_PHY_CNTL1      (0xE9 << 2)
#define HHI_HDMI_PHY_CNTL2      (0xEA << 2)
#define HHI_HDMI_PHY_CNTL3      (0xEB << 2)
#define HHI_HDMI_PHY_CNTL4      (0xEC << 2)
#define HHI_HDMI_PHY_STATUS     (0xED << 2)
#define HHI_VID_LOCK_CLK_CNTL   (0xF2 << 2)
#define HHI_BT656_CLK_CNTL      (0xF5 << 2)
#define HHI_SAR_CLK_CNTL        (0xF6 << 2)

#define ETH_MAC_REG_BASE         (0xc9410000)
#define ETH_MAC_REG_SIZE         (0x00010000)

#define DMC_CAV_LUT_DATAL           (0x12 << 2)
#define DMC_CAV_LUT_DATAH           (0x13 << 2)
#define DC_CAV_LUT_ADDR             (0x14 << 2)

#define DC_CAV_LUT_ADDR_INDEX_MASK  0x7
#define DC_CAV_LUT_ADDR_RD_EN       (1 << 8)
#define DC_CAV_LUT_ADDR_WR_EN       (2 << 8)
// Alternate Functions for Ethernet
#define S912_ETH_MDIO       S912_GPIOZ(0)
#define S912_ETH_MDIO_FN    1
#define S912_ETH_MDC        S912_GPIOZ(1)
#define S912_ETH_MDC_FN     1

#define S912_ETH_RGMII_RX_CLK        S912_GPIOZ(2)
#define S912_ETH_RGMII_RX_CLK_FN     1
#define S912_ETH_RX_DV               S912_GPIOZ(3)
#define S912_ETH_RX_DV_FN            1
#define S912_ETH_RXD0                S912_GPIOZ(4)
#define S912_ETH_RXD0_FN             1
#define S912_ETH_RXD1                S912_GPIOZ(5)
#define S912_ETH_RXD1_FN             1
#define S912_ETH_RXD2                S912_GPIOZ(6)
#define S912_ETH_RXD2_FN             1
#define S912_ETH_RXD3                S912_GPIOZ(7)
#define S912_ETH_RXD3_FN             1

#define S912_ETH_RGMII_TX_CLK        S912_GPIOZ(8)
#define S912_ETH_RGMII_TX_CLK_FN     1
#define S912_ETH_TX_EN               S912_GPIOZ(9)
#define S912_ETH_TX_EN_FN            1
#define S912_ETH_TXD0                S912_GPIOZ(10)
#define S912_ETH_TXD0_FN             1
#define S912_ETH_TXD1                S912_GPIOZ(11)
#define S912_ETH_TXD1_FN             1
#define S912_ETH_TXD2                S912_GPIOZ(12)
#define S912_ETH_TXD2_FN             1
#define S912_ETH_TXD3                S912_GPIOZ(13)
#define S912_ETH_TXD3_FN             1

// Alternate Functions for I2C
#define S912_I2C_SDA_A      S912_GPIODV(24)
#define S912_I2C_SDA_A_FN   2
#define S912_I2C_SCK_A      S912_GPIODV(25)
#define S912_I2C_SCK_A_FN   2

#define S912_I2C_SDA_B      S912_GPIODV(26)
#define S912_I2C_SDA_B_FN   2
#define S912_I2C_SCK_B      S912_GPIODV(27)
#define S912_I2C_SCK_B_FN   2

#define S912_I2C_SDA_C      S912_GPIODV(28)
#define S912_I2C_SDA_C_FN   2
#define S912_I2C_SCK_C      S912_GPIODV(29)
#define S912_I2C_SCK_C_FN   2

#define S912_I2C_SDA_D      S912_GPIOX(10)
#define S912_I2C_SDA_D_FN   3
#define S912_I2C_SCK_D      S912_GPIOX(11)
#define S912_I2C_SCK_D_FN   3

#define S912_I2C_SDA_AO     S912_GPIOAO(4)
#define S912_I2C_SDA_AO_FN  2
#define S912_I2C_SCK_AO     S912_GPIOAO(5)
#define S912_I2C_SCK_AO_FN  2

// Alternate functions for UARTs
#define S912_UART_TX_A      S912_GPIOX(12)
#define S912_UART_TX_A_FN   1
#define S912_UART_RX_A      S912_GPIOX(13)
#define S912_UART_RX_A_FN   1
#define S912_UART_CTS_A     S912_GPIOX(14)
#define S912_UART_CTS_A_FN  1
#define S912_UART_RTS_A     S912_GPIOX(15)
#define S912_UART_RTS_A_FN  1

#define S912_UART_TX_B      S912_GPIODV(24)
#define S912_UART_TX_B_FN   2
#define S912_UART_RX_B      S912_GPIODV(25)
#define S912_UART_RX_B_FN   2
#define S912_UART_CTS_B     S912_GPIODV(26)
#define S912_UART_CTS_B_FN  2
#define S912_UART_RTS_B     S912_GPIODV(27)
#define S912_UART_RTS_B_FN  2

#define S912_UART_TX_C      S912_GPIOX(8)
#define S912_UART_TX_C_FN   2
#define S912_UART_RX_C      S912_GPIOX(9)
#define S912_UART_RX_C_FN   2
#define S912_UART_CTS_C     S912_GPIOX(10)
#define S912_UART_CTS_C_FN  2
#define S912_UART_RTS_C     S912_GPIOX(11)
#define S912_UART_RTS_C_FN  2

#define S912_UART_TX_AO_A       S912_GPIOAO(0)
#define S912_UART_TX_AO_A_FN    1
#define S912_UART_RX_AO_A       S912_GPIOAO(1)
#define S912_UART_RX_AO_A_FN    1
#define S912_UART_CTS_AO_A      S912_GPIOAO(2)
#define S912_UART_CTS_AO_A_FN   1
#define S912_UART_RTS_AO_A      S912_GPIOAO(3)
#define S912_UART_RTS_AO_A_FN   1

// CTS/RTS cannot be used for UART_AO_B without interfering with UART_AO_A
#define S912_UART_TX_AO_B       S912_GPIOAO(4)
#define S912_UART_TX_AO_B_FN    1
#define S912_UART_RX_AO_B       S912_GPIOAO(5)
#define S912_UART_RX_AO_B_FN    1

#define S912_PWM_BASE               0xc1100000
#define S912_AO_PWM_BASE            0xc8100400

// PWM register offsets
// These are relative to base address 0xc1100000 and in sizeof(uint32_t)
#define S912_PWM_PWM_A              0x2154
#define S912_PWM_PWM_B              0x2155
#define S912_PWM_MISC_REG_AB        0x2156
#define S912_DS_A_B                 0x2157
#define S912_PWM_TIME_AB            0x2158
#define S912_PWM_A2                 0x2159
#define S912_PWM_PWM_C              0x2194
#define S912_PWM_PWM_D              0x2195
#define S912_PWM_MISC_REG_CD        0x2196
#define S912_PWM_DELTA_SIGMA_CD     0x2197
#define S912_PWM_PWM_E              0x21b0
#define S912_PWM_PWM_F              0x21b1
#define S912_PWM_MISC_REG_EF        0x21b2
#define S912_PWM_DELTA_SIGMA_EF     0x21b3
#define S912_PWM_TIME_EF            0x21b4
#define S912_PWM_E2                 0x21b5

// These are relative to base address 0xc8100400 and in sizeof(uint32_t)
#define S912_AO_PWM_PWM_A           0x54
#define S912_AO_PWM_PWM_B           0x55
#define S912_AO_PWM_MISC_REG_AB     0x56
#define S912_AO_PWM_DELTA_SIGMA_AB  0x57

// Alternate Functions for EMMC/NAND
#define S912_EMMC_NAND_D0       S912_GPIOBOOT(0)
#define S912_EMMC_NAND_D0_FN    1
#define S912_EMMC_NAND_D1       S912_GPIOBOOT(1)
#define S912_EMMC_NAND_D1_FN    1
#define S912_EMMC_NAND_D2       S912_GPIOBOOT(2)
#define S912_EMMC_NAND_D2_FN    1
#define S912_EMMC_NAND_D3       S912_GPIOBOOT(3)
#define S912_EMMC_NAND_D3_FN    1
#define S912_EMMC_NAND_D4       S912_GPIOBOOT(4)
#define S912_EMMC_NAND_D4_FN    1
#define S912_EMMC_NAND_D5       S912_GPIOBOOT(5)
#define S912_EMMC_NAND_D5_FN    1
#define S912_EMMC_NAND_D6       S912_GPIOBOOT(6)
#define S912_EMMC_NAND_D6_FN    1
#define S912_EMMC_NAND_D7       S912_GPIOBOOT(7)
#define S912_EMMC_NAND_D7_FN    1
#define S912_EMMC_CLK           S912_GPIOBOOT(8)
#define S912_EMMC_CLK_FN        1
#define S912_EMMC_RST           S912_GPIOBOOT(9)
#define S912_EMMC_RST_FN        1
#define S912_EMMC_CMD           S912_GPIOBOOT(10)
#define S912_EMMC_CMD_FN        1
#define S912_EMMC_DS            S912_GPIOBOOT(15)
#define S912_EMMC_DS_FN         1

// Reset register offsets
#define S912_RESET0_MASK              0x110
#define S912_RESET2_MASK              0x112
#define S912_RESET0_LEVEL             0x120
#define S912_RESET2_LEVEL             0x122

// Audio register blocks
#define S912_AUDIN_BASE         ((uint32_t)0xC110A000)
#define S912_AUDIN_LEN          ((uint32_t)0x1000)
#define S912_AUDOUT_BASE        ((uint32_t)0xC1105400)
#define S912_AUDOUT_LEN         ((uint32_t)0x400)

// Pin defs and alt functions for SPDIF in/out
#define S912_SPDIF_H4           S912_GPIOH(4)
#define S912_SPDIF_H4_OUT_FN    (1u)
#define S912_SPDIF_H4_IN_FN     (1u)
#define S912_SPDIF_AO6          S912_GPIOAO(6)
#define S912_SPDIF_AO6_OUT_FN   (3u)
#define S912_SPDIF_AO9          S912_GPIOAO(9)
#define S912_SPDIF_AO9_OUT_FN   (2u)
#define S912_SPDIF_Z14          S912_GPIOZ14(14)
#define S912_SPDIF_Z14_IN_FN    (2u)
