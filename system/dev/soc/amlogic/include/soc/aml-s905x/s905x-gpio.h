// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define S905X_GPIOX_PINS    19
#define S905X_GPIODV_PINS   30
#define S905X_GPIOH_PINS    10
#define S905X_GPIOBOOT_PINS 16
#define S905X_GPIOCARD_PINS 7
#define S905X_GPIOCLK_PINS  2
#define S905X_GPIOZ_PINS    16
#define S905X_GPIOAO_PINS   10

#define S905X_GPIOX_START       (0 * 32)
#define S905X_GPIODV_START      (1 * 32)
#define S905X_GPIOH_START       (2 * 32)
#define S905X_GPIOBOOT_START    (3 * 32)
#define S905X_GPIOCARD_START    (4 * 32)
#define S905X_GPIOCLK_START     (5 * 32)
#define S905X_GPIOZ_START       (6 * 32)
#define S905X_GPIOAO_START      (7 * 32)

#define S905X_GPIOX(n)      (S905X_GPIOX_START + n)
#define S905X_GPIODV(n)     (S905X_GPIODV_START + n)
#define S905X_GPIOH(n)      (S905X_GPIOH_START + n)
#define S905X_GPIOBOOT(n)   (S905X_GPIOBOOT_START + n)
#define S905X_GPIOCARD(n)   (S905X_GPIOCARD_START + n)
#define S905X_GPIOCLK(n)    (S905X_GPIOCLK_START + n)
#define S905X_GPIOZ(n)      (S905X_GPIOZ_START + n)
#define S905X_GPIOAO(n)     (S905X_GPIOAO_START + n)

#define S905X_GPIOX_0EN     0x18
#define S905X_GPIOX_OUT     0x19
#define S905X_GPIOX_IN      0x1a
#define S905X_GPIODV_0EN    0x0c
#define S905X_GPIODV_OUT    0x0d
#define S905X_GPIODV_IN     0x0e
#define S905X_GPIOH_0EN     0x0f
#define S905X_GPIOH_OUT     0x10
#define S905X_GPIOH_IN      0x11
#define S905X_GPIOBOOT_0EN  0x12
#define S905X_GPIOBOOT_OUT  0x13
#define S905X_GPIOBOOT_IN   0x14
#define S905X_GPIOCARD_0EN  0x12
#define S905X_GPIOCARD_OUT  0x13
#define S905X_GPIOCARD_IN   0x14
#define S905X_GPIOCLK_0EN   0x15
#define S905X_GPIOCLK_OUT   0x16
#define S905X_GPIOCLK_IN    0x17
#define S905X_GPIOZ_0EN     0x15
#define S905X_GPIOZ_OUT     0x16
#define S905X_GPIOZ_IN      0x17

#define S905X_PERIPHS_PIN_MUX_0 0x2C
#define S905X_PERIPHS_PIN_MUX_1 0x2D
#define S905X_PERIPHS_PIN_MUX_2 0x2E
#define S905X_PERIPHS_PIN_MUX_3 0x2F
#define S905X_PERIPHS_PIN_MUX_4 0x30
#define S905X_PERIPHS_PIN_MUX_5 0x31
#define S905X_PERIPHS_PIN_MUX_6 0x32
#define S905X_PERIPHS_PIN_MUX_7 0x33
#define S905X_PERIPHS_PIN_MUX_8 0x34
#define S905X_PERIPHS_PIN_MUX_9 0x35

// GPIO AO registers live in a seperate register bank.
#define S905X_AO_RTI_PIN_MUX_REG  0x05
#define S905X_AO_RTI_PIN_MUX_REG2 0x06
#define S905X_AO_GPIO_OEN_OUT     0x09   // OEN: [9:0], OUT: [25:16]
#define S905X_AO_GPIO_IN          0x0a

#define S905X_PULL_UP_REG0   0x3A
#define S905X_PULL_UP_REG1   0x3B
#define S905X_PULL_UP_REG2   0x3C
#define S905X_PULL_UP_REG3   0x3D
#define S905X_PULL_UP_REG4   0x3E

#define S905X_PULL_UP_EN_REG0   0x48
#define S905X_PULL_UP_EN_REG1   0x49
#define S905X_PULL_UP_EN_REG2   0x4A
#define S905X_PULL_UP_EN_REG3   0x4B
#define S905X_PULL_UP_EN_REG4   0x4C

// These are relative to base address 0xc1100000 and in sizeof(uint32_t)
#define S905X_GPIO_INT_STATUS            0x2618
#define S905X_GPIO_INT_CLEAR             0x2619
#define S905X_GPIO_INT_MASK              0x261A
#define S905X_GPIO_INT_EDGE_POLARITY     0x2620
#define S905X_GPIO_0_3_PIN_SELECT        0x2621
#define S905X_GPIO_4_7_PIN_SELECT        0x2622
#define S905X_GPIO_FILTER_SELECT         0x2623

#define S905X_GPIOA0_PIN_START      0
#define S905X_GPIOZ_PIN_START       10
#define S905X_GPIOH_PIN_START       26
#define S905X_GPIOBOOT_PIN_START    36
#define S905X_GPIOCARD_PIN_START    52
#define S905X_GPIODV_PIN_START      59
#define S905X_GPIOX_PIN_START       89
#define S905X_GPIOCLK_PIN_START     108
