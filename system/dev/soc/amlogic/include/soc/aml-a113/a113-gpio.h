// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define A113_GPIOX_START 0
#define A113_GPIOA_START 23
#define A113_GPIOBOOT_START (A113_GPIOA_START + 21)
#define A113_GPIOY_START (A113_GPIOBOOT_START + 15)
#define A113_GPIOZ_START (A113_GPIOY_START + 16)
#define A113_GPIOAO_START (A113_GPIOZ_START + 11)

#define A113_GPIOX(n) (A113_GPIOX_START + n)
#define A113_GPIOA(n) (A113_GPIOA_START + n)
#define A113_GPIOBOOT(n) (A113_GPIOBOOT_START + n)
#define A113_GPIOY(n) (A113_GPIOY_START + n)
#define A113_GPIOZ(n) (A113_GPIOZ_START + n)
#define A113_GPIOAO(n) (A113_GPIOAO_START + n)

#define A113_GPIO_REG0_EN_N 0x0c
#define A113_GPIO_REG0_O    0x0d
#define A113_GPIO_REG0_I    0x0e
#define A113_GPIO_REG1_EN_N 0x0f
#define A113_GPIO_REG1_O    0x10
#define A113_GPIO_REG1_I    0x11
#define A113_GPIO_REG2_EN_N 0x12
#define A113_GPIO_REG2_O    0x13
#define A113_GPIO_REG2_I    0x14
#define A113_GPIO_REG3_EN_N 0x15
#define A113_GPIO_REG3_O    0x16
#define A113_GPIO_REG3_I    0x17
#define A113_GPIO_REG4_EN_N 0x18
#define A113_GPIO_REG4_O    0x19
#define A113_GPIO_REG4_I    0x1a

#define A113_PERIPHS_PIN_MUX_0 0x20
#define A113_PERIPHS_PIN_MUX_1 0x21
#define A113_PERIPHS_PIN_MUX_2 0x22
#define A113_PERIPHS_PIN_MUX_3 0x23
#define A113_PERIPHS_PIN_MUX_4 0x24
#define A113_PERIPHS_PIN_MUX_5 0x25
#define A113_PERIPHS_PIN_MUX_6 0x26
// NOTE: A113_PERIPHS_PIN_MUX_7 is not specified by the manual
#define A113_PERIPHS_PIN_MUX_8 0x28
#define A113_PERIPHS_PIN_MUX_9 0x29
// NOTE: A113_PERIPHS_PIN_MUX_A is not specified by the manual
#define A113_PERIPHS_PIN_MUX_B 0x2b
#define A113_PERIPHS_PIN_MUX_C 0x2c
#define A113_PERIPHS_PIN_MUX_D 0x2d

// GPIO AO registers live in a seperate register bank.
#define A113_AO_RTI_PIN_MUX_REG0 0x05
#define A113_AO_RTI_PIN_MUX_REG1 0x06
#define A113_AO_GPIO_O_EN        0x08
#define A113_AO_GPIO_I           0x09
