// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

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

// GPIO AO registers live in a seperate register bank.
#define S912_AO_RTI_PIN_MUX_REG  0x05
#define S912_AO_RTI_PIN_MUX_REG2 0x06
#define S912_AO_GPIO_OEN_OUT     0x09   // OEN: [9:0], OUT: [25:16]
#define S912_AO_GPIO_IN          0x0a
