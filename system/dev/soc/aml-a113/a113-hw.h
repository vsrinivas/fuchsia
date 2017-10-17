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


// Pinmux Control Register Offsets
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

#define A113_PINMUX_ALT_FN_MAX 15