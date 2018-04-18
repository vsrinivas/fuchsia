// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <soc/aml-s905/s905-gpio.h>

static aml_gpio_block_t s905_gpio_blocks[] = {
    // GPIOX Block
    {
        .pin_count = S905_GPIOX_PINS,
        .oen_offset = S905_GPIOX_0EN,
        .input_offset = S905_GPIOX_IN,
        .output_offset = S905_GPIOX_OUT,
        .output_shift = 0,
        .mmio_index = 0,
        .pull_offset = S905_PULL_UP_REG4,
        .pull_en_offset = S905_PULL_UP_EN_REG4,
        .pin_start = S905_GPIOX_PIN_START,
        .lock = MTX_INIT,
    },
    // GPIOY Block
    {
        .pin_count = S905_GPIOY_PINS,
        .oen_offset = S905_GPIOY_0EN,
        .input_offset = S905_GPIOY_IN,
        .output_offset = S905_GPIOY_OUT,
        .output_shift = 0,
        .mmio_index = 0,
        .pull_offset = S905_PULL_UP_REG1,
        .pull_en_offset = S905_PULL_UP_EN_REG1,
        .pin_start = S905_GPIOY_PIN_START,
        .lock = MTX_INIT,
    },
    // GPIOZ Block
    {
        .pin_count = S905_GPIOZ_PINS,
        .oen_offset = S905_GPIOZ_0EN,
        .input_offset = S905_GPIOZ_IN,
        .output_offset = S905_GPIOZ_OUT,
        .output_shift = 0,
        .mmio_index = 0,
        .pull_offset = 0, // not supported
        .pull_en_offset = 0, // not supported
        .pin_start = S905_GPIOZ_PIN_START,
        .lock = MTX_INIT,
    },
    // GPIODV Block
    {
        .pin_count = S905_GPIODV_PINS,
        .oen_offset = S905_GPIODV_0EN,
        .input_offset = S905_GPIODV_IN,
        .output_offset = S905_GPIODV_OUT,
        .output_shift = 0,
        .mmio_index = 0,
        .pull_offset = S905_PULL_UP_REG0,
        .pull_en_offset = S905_PULL_UP_EN_REG0,
        .pin_start = S905_GPIODV_PIN_START,
        .lock = MTX_INIT,
    },
    // GPIOH Block
    {
        .pin_count = S905_GPIOH_PINS,
        .oen_offset = S905_GPIOH_0EN,
        .input_offset = S905_GPIOH_IN,
        .output_offset = S905_GPIOH_OUT,
        .output_shift = 20,
        .mmio_index = 0,
        .pull_offset = S905_PULL_UP_REG1,
        .pull_en_offset = S905_PULL_UP_EN_REG1,
        .pin_start = S905_GPIOH_PIN_START,
        .lock = MTX_INIT,
    },
    // GPIOCLK Block
    {
        .pin_count = S905_GPIOCLK_PINS,
        .oen_offset = S905_GPIOCLK_0EN,
        .input_offset = S905_GPIOCLK_IN,
        .output_offset = S905_GPIOCLK_OUT,
        .output_shift = 28,
        .mmio_index = 0,
        .pull_offset = S905_PULL_UP_REG3,
        .pull_en_offset = S905_PULL_UP_EN_REG3,
        .pin_start = S905_GPIOCLK_PIN_START,
        .lock = MTX_INIT,
    },
    // GPIOBOOT Block
    {
        .pin_count = S905_GPIOBOOT_PINS,
        .oen_offset = S905_GPIOBOOT_0EN,
        .input_offset = S905_GPIOBOOT_IN,
        .output_offset = S905_GPIOBOOT_OUT,
        .output_shift = 0,
        .mmio_index = 0,
        .pull_offset = S905_PULL_UP_REG2,
        .pull_en_offset = S905_PULL_UP_EN_REG2,
        .pin_start = S905_GPIOBOOT_PIN_START,
        .lock = MTX_INIT,
    },
    // GPIOCARD Block
    {
        .pin_count = S905_GPIOCARD_PINS,
        .oen_offset = S905_GPIOCARD_0EN,
        .input_offset = S905_GPIOCARD_IN,
        .output_offset = S905_GPIOCARD_OUT,
        .output_shift = 20,
        .mmio_index = 0,
        .pull_offset = S905_PULL_UP_REG2,
        .pull_en_offset = S905_PULL_UP_EN_REG2,
        .pin_start = S905_GPIOCARD_PIN_START,
        .lock = MTX_INIT,
    },
    // GPIOAO Block
    {
        .pin_count = S905_GPIOAO_PINS,
        .oen_offset = S905_AO_GPIO_OEN_OUT,
        .input_offset = S905_AO_GPIO_IN,
        .output_offset = S905_AO_GPIO_OEN_OUT,
        .output_shift = 0,
        .output_write_shift = 16, // output is shared with OEN
        .mmio_index = 1,
        .pull_offset = S905_PULL_UP_REG_AO,
        .pull_en_offset = S905_PULL_UP_EN_REGAO,
        .pin_start = S905_GPIOA0_PIN_START,
        .lock = MTX_INIT,
    },
};

#define REG_0 S905_PERIPHS_PIN_MUX_0
#define REG_1 S905_PERIPHS_PIN_MUX_1
#define REG_2 S905_PERIPHS_PIN_MUX_2
#define REG_3 S905_PERIPHS_PIN_MUX_3
#define REG_4 S905_PERIPHS_PIN_MUX_4
#define REG_5 S905_PERIPHS_PIN_MUX_5
#define REG_6 S905_PERIPHS_PIN_MUX_6
#define REG_7 S905_PERIPHS_PIN_MUX_7
#define REG_8 S905_PERIPHS_PIN_MUX_8
#define REG_9 S905_PERIPHS_PIN_MUX_9
#define AO_REG S905_AO_RTI_PIN_MUX_REG
#define AO_REG_2 S905_AO_RTI_PIN_MUX_REG2

static aml_gpio_interrupt_t s905_interrupt_block = {
    .pin_0_3_select_offset =    S905_GPIO_0_3_PIN_SELECT,
    .pin_4_7_select_offset =    S905_GPIO_4_7_PIN_SELECT,
    .edge_polarity_offset =     S905_GPIO_INT_EDGE_POLARITY,
    .filter_select_offset =     S905_GPIO_FILTER_SELECT,
    .status_offset =            S905_GPIO_INT_STATUS,
    .mask_offset =              S905_GPIO_INT_MASK,
};

static const aml_pinmux_block_t s905_pinmux_blocks[] = {
    // GPIOX Block
    {
        .mux = {
            { .regs = { REG_8 }, .bits = { 5 }, },
            { .regs = { REG_8 }, .bits = { 4 }, },
            { .regs = { REG_8 }, .bits = { 3 }, },
            { .regs = { REG_8 }, .bits = { 2 }, },
            { .regs = { REG_8 }, .bits = { 1 }, },
            { .regs = { REG_8 }, .bits = { 0 }, },
            { .regs = { 0, 0, 0, REG_3, REG_3 }, .bits = { 0, 0, 0, 9, 17 }, },
            { .regs = { REG_8, 0, 0, REG_3, REG_3 }, .bits = { 11, 0, 0, 8, 18 }, },
            { .regs = { REG_4, 0, REG_3, REG_3 }, .bits = { 7, 0, 30, 10 }, },
            { .regs = { REG_4, 0, REG_3, REG_3 }, .bits = { 6, 0, 29, 7 }, },
            { .regs = { 0, 0, REG_3 }, .bits = { 0, 0, 28 }, },
            { .regs = { 0, 0, REG_3 }, .bits = { 0, 0, 27 }, },
            { .regs = { 0, REG_4, REG_4 }, .bits = { 0, 13, 17 }, },
            { .regs = { 0, REG_4, REG_4 }, .bits = { 0, 12, 16 }, },
            { .regs = { 0, REG_4, REG_4 }, .bits = { 0, 11, 15 }, },
            { .regs = { 0, REG_4, REG_4 }, .bits = { 0, 10, 14 }, },
            { .regs = { 0, REG_2, 0, 0, REG_2 }, .bits = { 0, 22, 0, 0, 30 }, },
        },
    },
    // GPIOY Block
    {
        .mux = {
            { .regs = { REG_2, REG_3, 0, 0, REG_1 }, .bits = { 19, 2, 0, 0, 0 }, },
            { .regs = { REG_2, REG_3, 0, 0, REG_1 }, .bits = { 18, 1, 0, 0, 1 }, },
            { .regs = { REG_2, REG_3 }, .bits = { 17, 0 }, },
            { .regs = { REG_2, REG_3, 0, 0, REG_1 }, .bits = { 16, 4, 0, 0, 1 }, },
            { .regs = { REG_2, REG_3, 0, REG_1 }, .bits = { 16, 5, 0, 12 }, },
            { .regs = { REG_2, REG_3, 0, REG_1 }, .bits = { 16, 5, 0, 13 }, },
            { .regs = { REG_2, REG_3, 0, 0, REG_1 }, .bits = { 16, 5, 0, 0, 3 }, },
            { .regs = { REG_2, REG_3, 0, 0, REG_1 }, .bits = { 16, 5, 0, 0, 4 }, },
            { .regs = { REG_2, REG_3, 0, 0, REG_1 }, .bits = { 16, 5, 0, 0, 5 }, },
            { .regs = { REG_2, REG_3, 0, 0, REG_1 }, .bits = { 16, 5, 0, 0, 6 }, },
            { .regs = { REG_2, REG_3, 0, 0, REG_1 }, .bits = { 16, 5, 0, 0, 7 }, },
            { .regs = { 0, REG_3, REG_1, 0, REG_1 }, .bits = { 0, 3, 19, 0, 8 }, },
            { .regs = { 0, 0, REG_1, 0, REG_1 }, .bits = { 0, 0, 18, 0, 9 }, },
            { .regs = { 0, 0, REG_1, 0, REG_1 }, .bits = { 0, 0, 17, 0, 10 }, },
            { .regs = { 0, 0, REG_1, 0, REG_1 }, .bits = { 0, 0, 16, 0, 11 }, },
            { .regs = { REG_2, 0, 0, REG_1, REG_1 }, .bits = { 20, 0, 0, 20, 22 }, },
            { .regs = { REG_2, 0, 0, REG_1 }, .bits = { 21, 0, 0, 21 }, },
        },
    },
    // GPIOZ Block
    {
        .mux = {
            { .regs = { REG_6, REG_5 }, .bits = { 1, 5 }, },
            { .regs = { REG_6, REG_5 }, .bits = { 0, 6 }, },
            { .regs = { REG_6 }, .bits = { 13 }, },
            { .regs = { REG_6, REG_5 }, .bits = { 12, 7 }, },
            { .regs = { REG_6, REG_5 }, .bits = { 11, 4 }, },
            { .regs = { REG_6, REG_5 }, .bits = { 10, 4 }, },
            { .regs = { REG_6, REG_5, REG_5, REG_5, REG_4 }, .bits = { 9, 4, 27, 9 }, },
            { .regs = { REG_6, REG_5, REG_5, REG_5, REG_4 }, .bits = { 8, 4, 26, 8 }, },
            { .regs = { REG_6, REG_5 }, .bits = { 7, 4 }, },
            { .regs = { REG_6, REG_5 }, .bits = { 6, 4 }, },
            { .regs = { REG_6, REG_5 }, .bits = { 5, 4 }, },
            { .regs = { REG_6, REG_5 }, .bits = { 4, 4 }, },
            { .regs = { REG_6, 0, REG_5 }, .bits = { 3, 0, 28 }, },
            { .regs = { REG_6, 0, REG_5 }, .bits = { 2, 0, 29 }, },
            { },
            { .regs = { 0, REG_6 }, .bits = { 0, 15 }, },
        },
    },
    // GPIODV Block
    {
        .mux = {
            { },
            { },
            { },
            { },
            { },
            { },
            { },
            { },
            { },
            { },
            { },
            { },
            { },
            { },
            { },
            { },
            { },
            { },
            { },
            { },
            { },
            { },
            { },
            { },
            { .regs = { REG_0, REG_0, REG_5, 0, REG_2, REG_7 }, .bits = { 7, 12, 12, 0, 29, 26 }, },
            { .regs = { REG_0, REG_0, REG_5, 0, REG_2, REG_7 }, .bits = { 6, 11, 11, 0, 28, 27 }, },
            { .regs = { 0, REG_0, REG_5, 0, REG_2, REG_7 }, .bits = { 0, 10, 10, 0, 27, 24 }, },
            { .regs = { 0, REG_0, REG_5, REG_5, REG_2, REG_7 }, .bits = { 0, 9, 9, 8, 26, 25 }, },
            { .regs = { 0, 0, 0, 0, REG_3, REG_7 }, .bits = { 0, 0, 0, 0, 20, 22 }, },
            { .regs = { 0, 0, 0, REG_3, REG_3, REG_7 }, .bits = { 0, 0, 0, 22, 21, 23 }, },
        },
    },
    // GPIOH Block
    {
        .mux = {
            { .regs = { REG_1 }, .bits = { 26 }, },
            { .regs = { REG_1 }, .bits = { 25 }, },
            { .regs = { REG_1 }, .bits = { 24 }, },
            {},
        },
    },
    // GPIOCLK Block
    {
        .mux = {
           {},
           {},
        },
    },
    // GPIOBOOT Block
    {
        .mux = {
            { .regs = { 0, REG_4 }, .bits = { 0, 30 }, },
            { .regs = { 0, REG_4 }, .bits = { 0, 30 }, },
            { .regs = { 0, REG_4 }, .bits = { 0, 30 }, },
            { .regs = { 0, REG_4 }, .bits = { 0, 30 }, },
            { .regs = { 0, REG_4 }, .bits = { 0, 30 }, },
            { .regs = { 0, REG_4 }, .bits = { 0, 30 }, },
            { .regs = { 0, REG_4 }, .bits = { 0, 30 }, },
            { .regs = { 0, REG_4 }, .bits = { 0, 30 }, },
            { .regs = { REG_4, REG_4 }, .bits = { 26, 18 }, },
            { .regs = { REG_4 }, .bits = { 27 }, },
            { .regs = { REG_4, REG_4 }, .bits = { 25, 19 }, },
            { .regs = { REG_4, 0, REG_5 }, .bits = { 24, 0, 1 }, },
            { .regs = { REG_4, 0, REG_5 }, .bits = { 23, 0, 3 }, },
            { .regs = { REG_4, 0, REG_5 }, .bits = { 22, 0, 2 }, },
            { .regs = { REG_4 }, .bits = { 21 }, },
            { .regs = { REG_4, 0, REG_5 }, .bits = { 20, 0, 3 }, },
         },
    },
    // GPIOCARD Block
    {
        .mux = {
            { .regs = { REG_2 }, .bits = { 14 }, },
            { .regs = { REG_2 }, .bits = { 15 }, },
            { .regs = { REG_2 }, .bits = { 11 }, },
            { .regs = { REG_2 }, .bits = { 10 }, },
            { .regs = { REG_2, REG_8, REG_8 }, .bits = { 12, 10, 18 }, },
            { .regs = { REG_2, REG_8, REG_8 }, .bits = { 13, 17, 9 }, },
        },
    },
    // GPIOAO Block
    {
        .mux = {
            { .regs = { AO_REG, AO_REG }, .bits = { 12, 26 }, },
            { .regs = { AO_REG, AO_REG }, .bits = { 11, 25 }, },
            { .regs = { AO_REG, AO_REG }, .bits = { 10, 8 }, },
            { .regs = { AO_REG, AO_REG, AO_REG }, .bits = { 9, 7, 22 }, },
            { .regs = { 0, AO_REG, AO_REG, AO_REG }, .bits = { 0, 24, 6, 2 }, },
            { .regs = { 0, AO_REG, AO_REG, AO_REG }, .bits = { 0, 25, 5, 1 }, },
            { .regs = { 0, AO_REG, AO_REG, AO_REG }, .bits = { 0, 0, 18, 16 }, },
            { .regs = { AO_REG, AO_REG }, .bits = { 0, 2 }, },
            { .regs = { 0, 0, 0, AO_REG }, .bits = { 0, 0, 0, 30 }, },
            { .regs = { 0, 0, 0, AO_REG }, .bits = { 0, 0, 0, 29 }, },
            { .regs = { 0, 0, 0, AO_REG }, .bits = { 0, 0, 0, 28 }, },
            { .regs = { 0, 0, 0, AO_REG }, .bits = { 0, 0, 0, 27 }, },
            { .regs = { AO_REG, AO_REG, AO_REG, AO_REG_2 }, .bits = { 15, 14, 17, 0 }, },
            { .regs = { AO_REG, AO_REG, AO_REG, AO_REG_2 }, .bits = { 31, 4, 3, 2 }, },
        },
    },
};

static_assert(countof(s905_gpio_blocks) == countof(s905_pinmux_blocks), "");

#undef REG_0
#undef REG_1
#undef REG_2
#undef REG_3
#undef REG_4
#undef REG_5
#undef REG_6
#undef REG_7
#undef REG_8
#undef REG_9
#undef AO_REG
#undef AO_REG_2
