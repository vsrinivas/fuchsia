// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Display Related Register
#define S912_PRESET_BASE                0xC1104000
#define S912_PRESET_LENGTH              0x1000
#define S912_DMC_REG_BASE               0xC8838000
#define S912_DMC_REG_LENGTH             0x1000
#define S912_HDMITX_BASE                0xC883A000
#define S912_HDMITX_LENGTH              0x2000
#define S912_HIU_BASE                   0xC883C000
#define S912_HIU_LENGTH                 0x2000
#define S912_VPU_BASE                   0xD0100000
#define S912_VPU_LENGTH                 0x40000
#define S912_HDMITX_SEC_BASE            0xDA83A000
#define S912_HDMITX_SEC_LENGTH          0x2000

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
