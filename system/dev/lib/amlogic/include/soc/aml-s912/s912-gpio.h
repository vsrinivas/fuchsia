// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define GPIO_INTERRUPT_POLARITY_SHIFT   16
#define PINS_PER_BLOCK                  32
#define ALT_FUNCTION_MAX                6
#define MAX_GPIO_INDEX                  255
#define BITS_PER_GPIO_INTERRUPT         8

#define S912_GPIOX_PINS     19
#define S912_GPIODV_PINS    30
#define S912_GPIOH_PINS     10
#define S912_GPIOBOOT_PINS  16
#define S912_GPIOCARD_PINS  7
#define S912_GPIOCLK_PINS   2
#define S912_GPIOZ_PINS     16
#define S912_GPIOAO_PINS    10

#define S912_GPIOX_START    (0 * 32)
#define S912_GPIODV_START   (1 * 32)
#define S912_GPIOH_START    (2 * 32)
#define S912_GPIOBOOT_START (3 * 32)
#define S912_GPIOCARD_START (4 * 32)
#define S912_GPIOCLK_START  (5 * 32)
#define S912_GPIOZ_START    (6 * 32)
#define S912_GPIOAO_START   (7 * 32)

#define S912_GPIOX(n)       (S912_GPIOX_START + n)
#define S912_GPIODV(n)      (S912_GPIODV_START + n)
#define S912_GPIOH(n)      (S912_GPIOH_START + n)
#define S912_GPIOBOOT(n)    (S912_GPIOBOOT_START + n)
#define S912_GPIOCARD(n)    (S912_GPIOCARD_START + n)
#define S912_GPIOCLK(n)     (S912_GPIOCLK_START + n)
#define S912_GPIOZ(n)       (S912_GPIOZ_START + n)
#define S912_GPIOAO(n)      (S912_GPIOAO_START + n)

#define S912_GPIOX_0EN      0x18
#define S912_GPIOX_OUT      0x19
#define S912_GPIOX_IN       0x1a
#define S912_GPIODV_0EN     0x0c
#define S912_GPIODV_OUT     0x0d
#define S912_GPIODV_IN      0x0e
#define S912_GPIOH_0EN      0x0f
#define S912_GPIOH_OUT      0x10
#define S912_GPIOH_IN       0x11
#define S912_GPIOBOOT_0EN   0x12
#define S912_GPIOBOOT_OUT   0x13
#define S912_GPIOBOOT_IN    0x14
#define S912_GPIOCARD_0EN   0x12
#define S912_GPIOCARD_OUT   0x13
#define S912_GPIOCARD_IN    0x14
#define S912_GPIOCLK_0EN    0x15
#define S912_GPIOCLK_OUT    0x16
#define S912_GPIOCLK_IN     0x17
#define S912_GPIOZ_0EN      0x15
#define S912_GPIOZ_OUT      0x16
#define S912_GPIOZ_IN       0x17

#define S912_PERIPHS_PIN_MUX_0 0x2C
#define S912_PERIPHS_PIN_MUX_1 0x2D
#define S912_PERIPHS_PIN_MUX_2 0x2E
#define S912_PERIPHS_PIN_MUX_3 0x2F
#define S912_PERIPHS_PIN_MUX_4 0x30
#define S912_PERIPHS_PIN_MUX_5 0x31
#define S912_PERIPHS_PIN_MUX_6 0x32
#define S912_PERIPHS_PIN_MUX_7 0x33
#define S912_PERIPHS_PIN_MUX_8 0x34
#define S912_PERIPHS_PIN_MUX_9 0x35

#define S912_PULL_UP_REG0   0x3A
#define S912_PULL_UP_REG1   0x3B
#define S912_PULL_UP_REG2   0x3C
#define S912_PULL_UP_REG3   0x3D
#define S912_PULL_UP_REG4   0x3E

#define S912_PULL_UP_EN_REG0   0x48
#define S912_PULL_UP_EN_REG1   0x49
#define S912_PULL_UP_EN_REG2   0x4A
#define S912_PULL_UP_EN_REG3   0x4B
#define S912_PULL_UP_EN_REG4   0x4C

// These are relative to base address 0xc1100000 and in sizeof(uint32_t)
#define S912_GPIO_INT_STATUS            0x2618
#define S912_GPIO_INT_CLEAR             0x2619
#define S912_GPIO_INT_MASK              0x261A
#define S912_GPIO_INT_EDGE_POLARITY     0x2620
#define S912_GPIO_0_3_PIN_SELECT        0x2621
#define S912_GPIO_4_7_PIN_SELECT        0x2622
#define S912_GPIO_FILTER_SELECT         0x2623

#define S912_GPIOA0_PIN_START      0
#define S912_GPIOZ_PIN_START       10
#define S912_GPIOH_PIN_START       26
#define S912_GPIOBOOT_PIN_START    36
#define S912_GPIOCARD_PIN_START    52
#define S912_GPIODV_PIN_START      59
#define S912_GPIOX_PIN_START       89
#define S912_GPIOCLK_PIN_START     108

// GPIO AO registers live in a seperate register bank.
#define S912_AO_RTI_PIN_MUX_REG  0x05
#define S912_AO_RTI_PIN_MUX_REG2 0x06
#define S912_AO_GPIO_OEN_OUT     0x09   // OEN: [9:0], OUT: [25:16]
#define S912_AO_GPIO_IN          0x0a
#define S912_A0_GPIO_OUT_OFFSET  16

// Alternate Functions for SDIO
#define S912_WIFI_SDIO_D0           S912_GPIOX(0)
#define S912_WIFI_SDIO_D0_FN        1
#define S912_WIFI_SDIO_D1           S912_GPIOX(1)
#define S912_WIFI_SDIO_D1_FN        1
#define S912_WIFI_SDIO_D2           S912_GPIOX(2)
#define S912_WIFI_SDIO_D2_FN        1
#define S912_WIFI_SDIO_D3           S912_GPIOX(3)
#define S912_WIFI_SDIO_D3_FN        1
#define S912_WIFI_SDIO_CLK          S912_GPIOX(4)
#define S912_WIFI_SDIO_CLK_FN       1
#define S912_WIFI_SDIO_CMD          S912_GPIOX(5)
#define S912_WIFI_SDIO_CMD_FN       1
#define S912_WIFI_SDIO_WAKE_HOST    S912_GPIOX(7)
#define S912_WIFI_SDIO_WAKE_HOST_FN 1
