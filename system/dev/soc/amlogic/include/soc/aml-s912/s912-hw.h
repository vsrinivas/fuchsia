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

// IRQs
#define S912_M_I2C_0_IRQ                53
#define S912_UART_A_IRQ                 58
#define S912_USBH_IRQ                   62
#define S912_USBD_IRQ                   63
#define S912_M_I2C_3_IRQ                71
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
#define S912_M_I2C_1_IRQ                246
#define S912_M_I2C_2_IRQ                247

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

