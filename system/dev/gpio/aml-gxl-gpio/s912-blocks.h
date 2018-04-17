// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <soc/aml-s912/s912-gpio.h>

static aml_gpio_block_t s912_gpio_blocks[] = {
    // GPIOX Block
    {
        .pin_count = S912_GPIOX_PINS,
        .oen_offset = S912_GPIOX_0EN,
        .input_offset = S912_GPIOX_IN,
        .output_offset = S912_GPIOX_OUT,
        .output_shift = 0,
        .mmio_index = 0,
        .pull_offset = S912_PULL_UP_REG4,
        .pull_en_offset = S912_PULL_UP_EN_REG4,
        .pin_start = S912_GPIOX_PIN_START,
        .lock = MTX_INIT,
    },
    // GPIODV Block
    {
        .pin_count = S912_GPIODV_PINS,
        .oen_offset = S912_GPIODV_0EN,
        .input_offset = S912_GPIODV_IN,
        .output_offset = S912_GPIODV_OUT,
        .output_shift = 0,
        .mmio_index = 0,
        .pull_offset = S912_PULL_UP_REG0,
        .pull_en_offset = S912_PULL_UP_EN_REG0,
        .pin_start = S912_GPIODV_PIN_START,
        .lock = MTX_INIT,
    },
    // GPIOH Block
    {
        .pin_count = S912_GPIOH_PINS,
        .oen_offset = S912_GPIOH_0EN,
        .input_offset = S912_GPIOH_IN,
        .output_offset = S912_GPIOH_OUT,
        .output_shift = 20,
        .mmio_index = 0,
        .pull_offset = S912_PULL_UP_REG1,
        .pull_en_offset = S912_PULL_UP_EN_REG1,
        .pin_start = S912_GPIOH_PIN_START,
        .lock = MTX_INIT,
    },
    // GPIOBOOT Block
    {
        .pin_count = S912_GPIOBOOT_PINS,
        .oen_offset = S912_GPIOBOOT_0EN,
        .input_offset = S912_GPIOBOOT_IN,
        .output_offset = S912_GPIOBOOT_OUT,
        .output_shift = 0,
        .mmio_index = 0,
        .pull_offset = S912_PULL_UP_REG2,
        .pull_en_offset = S912_PULL_UP_EN_REG2,
        .pin_start = S912_GPIOBOOT_PIN_START,
        .lock = MTX_INIT,
    },
    // GPIOCARD Block
    {
        .pin_count = S912_GPIOCARD_PINS,
        .oen_offset = S912_GPIOCARD_0EN,
        .input_offset = S912_GPIOCARD_IN,
        .output_offset = S912_GPIOCARD_OUT,
        .output_shift = 20,
        .mmio_index = 0,
        .pull_offset = S912_PULL_UP_REG2,
        .pull_en_offset = S912_PULL_UP_EN_REG2,
        .pin_start = S912_GPIOCARD_PIN_START,
        .lock = MTX_INIT,
    },
    // GPIOCLK Block
    {
        .pin_count = S912_GPIOCLK_PINS,
        .oen_offset = S912_GPIOCLK_0EN,
        .input_offset = S912_GPIOCLK_IN,
        .output_offset = S912_GPIOCLK_OUT,
        .output_shift = 28,
        .mmio_index = 0,
        .pull_offset = S912_PULL_UP_REG3,
        .pull_en_offset = S912_PULL_UP_EN_REG3,
        .pin_start = S912_GPIOCLK_PIN_START,
        .lock = MTX_INIT,
    },
    // GPIOZ Block
    {
        .pin_count = S912_GPIOZ_PINS,
        .oen_offset = S912_GPIOZ_0EN,
        .input_offset = S912_GPIOZ_IN,
        .output_offset = S912_GPIOZ_OUT,
        .output_shift = 0,
        .mmio_index = 0,
        .pull_offset = S912_PULL_UP_REG3,
        .pull_en_offset = S912_PULL_UP_EN_REG3,
        .pin_start = S912_GPIOZ_PIN_START,
        .lock = MTX_INIT,
    },
    // GPIOAO Block
    {
        .pin_count = S912_GPIOAO_PINS,
        .oen_offset = S912_AO_GPIO_OEN_OUT,
        .input_offset = S912_AO_GPIO_IN,
        .output_offset = S912_AO_GPIO_OEN_OUT,
        .output_shift = 0,
        .output_write_shift = 16, //OUT/EN share reg
        .mmio_index = 1,
        .pull_offset = 0, // not supported
        .pull_en_offset = 0, // not supported
        .pin_start = S912_GPIOA0_PIN_START,
        .lock = MTX_INIT,
    },
};

#define REG_0 S912_PERIPHS_PIN_MUX_0
#define REG_1 S912_PERIPHS_PIN_MUX_1
#define REG_2 S912_PERIPHS_PIN_MUX_2
#define REG_3 S912_PERIPHS_PIN_MUX_3
#define REG_4 S912_PERIPHS_PIN_MUX_4
#define REG_5 S912_PERIPHS_PIN_MUX_5
#define REG_6 S912_PERIPHS_PIN_MUX_6
#define REG_7 S912_PERIPHS_PIN_MUX_7
#define REG_8 S912_PERIPHS_PIN_MUX_8
#define REG_9 S912_PERIPHS_PIN_MUX_9
#define AO_REG S912_AO_RTI_PIN_MUX_REG
#define AO_REG_2 S912_AO_RTI_PIN_MUX_REG2

static aml_gpio_interrupt_t s912_interrupt_block = {
    .pin_0_3_select_offset =    S912_GPIO_0_3_PIN_SELECT,
    .pin_4_7_select_offset =    S912_GPIO_4_7_PIN_SELECT,
    .edge_polarity_offset =     S912_GPIO_INT_EDGE_POLARITY,
    .filter_select_offset =     S912_GPIO_FILTER_SELECT,
    .status_offset =            S912_GPIO_INT_STATUS,
    .mask_offset =              S912_GPIO_INT_MASK,
};

static const aml_pinmux_block_t s912_pinmux_blocks[] = {
    // GPIOX Block
    {
        .mux = {
            { .regs = { REG_5 }, .bits = { 31 }, },
            { .regs = { REG_5 }, .bits = { 30 }, },
            { .regs = { REG_5 }, .bits = { 29 }, },
            { .regs = { REG_5 }, .bits = { 28 }, },
            { .regs = { REG_5 }, .bits = { 27 }, },
            { .regs = { REG_5 }, .bits = { 26 }, },
            { .regs = { REG_5 }, .bits = { 25 }, },
            { .regs = { REG_5, REG_5 }, .bits = { 24, 14 }, },
            { .regs = { REG_5, REG_5, 0, REG_5 }, .bits = { 23, 13, 0, 3 }, },
            { .regs = { REG_5, REG_5, 0, REG_5 }, .bits = { 22, 12, 0, 2 }, },
            { .regs = { REG_5, REG_5, REG_5, REG_5 }, .bits = { 21, 11, 5, 1 }, },
            { .regs = { REG_5, REG_5, REG_5, REG_5 }, .bits = { 20, 10, 4, 0 }, },
            { .regs = { REG_5 }, .bits = { 19 }, },
            { .regs = { REG_5 }, .bits = { 18 }, },
            { .regs = { REG_5 }, .bits = { 17 }, },
            { .regs = { REG_5 }, .bits = { 16 }, },
            { .regs = { REG_5 }, .bits = { 15 }, },
            // pinmux not specified for GPIOX_17 and GPIOX_18.
        },
    },
    // GPIODV Block
    {
        .mux = {
            { .regs = { REG_3, 0, 0, REG_2, REG_1 }, .bits = { 10, 0, 0, 4, 8 }, },
            { .regs = { REG_3, 0, 0, REG_2 }, .bits = { 10, 0, 0, 3 }, },
            { .regs = { REG_3, 0, 0, REG_2 }, .bits = { 9, 0, 0, 3 }, },
            { .regs = { REG_3, 0, 0, REG_2 }, .bits = { 9, 0, 0, 3 }, },
            { .regs = { REG_3, 0, 0, REG_2 }, .bits = { 9, 0, 0, 3 }, },
            { .regs = { REG_3, 0, 0, REG_2 }, .bits = { 9, 0, 0, 3 }, },
            { .regs = { REG_3, 0, 0, REG_2 }, .bits = { 9, 0, 0, 3 }, },
            { .regs = { REG_3, 0, 0, REG_2 }, .bits = { 9, 0, 0, 3 }, },
            { .regs = { REG_3, 0, 0, REG_2 }, .bits = { 8, 0, 0, 2 }, },
            { .regs = { REG_3, 0, 0, REG_2 }, .bits = { 8, 0, 0, 1 }, },
            { .regs = { REG_3, 0, 0, REG_2 }, .bits = { 7, 0, 0, 0 }, },
            { .regs = { REG_3, 0, 0, REG_1 }, .bits = { 7, 0, 0, 31 }, },
            { .regs = { REG_3, 0, 0, REG_1 }, .bits = { 7, 0, 0, 30 }, },
            { .regs = { REG_3, 0, 0, REG_1 }, .bits = { 7, 0, 0, 29 }, },
            { .regs = { REG_3, 0, 0, REG_1 }, .bits = { 7, 0, 0, 28 }, },
            { .regs = { REG_3, 0, 0, REG_1 }, .bits = { 7, 0, 0, 27 }, },
            { .regs = { REG_3, 0, 0, REG_1, REG_1 }, .bits = { 6, 0, 0, 26, 24 }, },
            { .regs = { REG_3, 0, 0, REG_1, REG_1 }, .bits = { 6, 0, 0, 25, 23 }, },
            { .regs = { REG_3, REG_1, 0, REG_1 }, .bits = { 5, 17, 0, 25 }, },
            { .regs = { REG_3, REG_1, 0, REG_1 }, .bits = { 5, 16, 0, 25 }, },
            { .regs = { REG_3, 0, 0, REG_1 }, .bits = { 5, 0, 0, 25 }, },
            { .regs = { REG_3, 0, 0, REG_1 }, .bits = { 5, 0, 0, 25 }, },
            { .regs = { REG_3, 0, REG_2, REG_1 }, .bits = { 5, 0, 18, 25 }, },
            { .regs = { REG_3, 0, REG_2, REG_1 }, .bits = { 5, 0, 17, 25 }, },
            { .regs = { REG_3, REG_1, REG_2, REG_2, REG_1 }, .bits = { 4, 15, 16, 7, 22 }, },
            { .regs = { REG_3, REG_1, REG_2, REG_2, REG_1 }, .bits = { 3, 14, 15, 6, 21 }, },
            { .regs = { REG_1, REG_1, REG_2 }, .bits = { 20, 13, 14 }, },
            { .regs = { REG_1, REG_1, REG_2, 0, REG_1 }, .bits = { 18, 12, 13, 0, 19 }, },
            { .regs = { REG_2, REG_1, 0, REG_1 }, .bits = { 12, 11, 0, 9 }, },
            { .regs = { REG_2, REG_1, REG_2 }, .bits = { 11, 10, 5 }, },
        },
    },
    // GPIOH Block
    {
        .mux = {
            { .regs = { REG_6 }, .bits = { 31 }, },
            { .regs = { REG_6 }, .bits = { 30 }, },
            { .regs = { REG_6 }, .bits = { 29 }, },
            {},
            { .regs = { REG_6, REG_6 }, .bits = { 28, 27 }, },
            {},
            { .regs = { 0, 0, REG_6, 0, REG_6 }, .bits = { 0, 0, 26, 0, 20 }, },
            { .regs = { 0, 0, REG_6, REG_6, REG_6 }, .bits = { 0, 0, 25, 22, 19 }, },
            { .regs = { 0, 0, REG_6, REG_6, REG_6 }, .bits = { 0, 0, 24, 21, 18 }, },
            { .regs = { 0, 0, REG_6, 0, REG_6 }, .bits = { 0, 0, 23, 0, 17 }, },
        },
    },
    // GPIOBOOT Block
    {
        .mux = {
            { .regs = { REG_7 }, .bits = { 31 }, },
            { .regs = { REG_7 }, .bits = { 31 }, },
            { .regs = { REG_7 }, .bits = { 31 }, },
            { .regs = { REG_7 }, .bits = { 31 }, },
            { .regs = { REG_7 }, .bits = { 31 }, },
            { .regs = { REG_7 }, .bits = { 31 }, },
            { .regs = { REG_7 }, .bits = { 31 }, },
            { .regs = { REG_7 }, .bits = { 31 }, },
            { .regs = { REG_7, REG_7 }, .bits = { 30, 7 }, },
            { .regs = { 0, REG_7 }, .bits = { 0, 6 }, },
            { .regs = { REG_7, REG_7 }, .bits = { 29, 5 }, },
            { .regs = { 0, REG_7, REG_7 }, .bits = { 0, 4, 13 }, },
            { .regs = { 0, REG_7, REG_7 }, .bits = { 0, 3, 12 }, },
            { .regs = { 0, REG_7, REG_7 }, .bits = { 0, 2, 11 }, },
            { .regs = { 0, REG_7 }, .bits = { 0, 1 }, },
            { .regs = { REG_7, REG_7, REG_7 }, .bits = { 28, 0, 10 }, },
        },
    },
    // GPIOCARD Block
    {
        .mux = {
            { .regs = { REG_6 }, .bits = { 5 }, },
            { .regs = { REG_6 }, .bits = { 4 }, },
            { .regs = { REG_6 }, .bits = { 3 }, },
            { .regs = { REG_6 }, .bits = { 2 }, },
            { .regs = { REG_6, REG_6, REG_6 }, .bits = { 1, 9, 11 }, },
            { .regs = { REG_6, REG_6, REG_6 }, .bits = { 0, 8, 10 }, },
        },
    },
    // GPIOCLK Block
    {
        .mux = {
           { .regs = { 0, 0, REG_8 }, .bits = { 0, 0, 31 }, },
           { .regs = { 0, REG_8, REG_8 }, .bits = { 0, 30, 29 }, },
        },
    },
    // GPIOZ Block
    {
        .mux = {
            { .regs = { REG_4, REG_3, REG_3, REG_3 }, .bits = { 23, 14, 31, 19 }, },
            { .regs = { REG_4, REG_3, REG_3, REG_3 }, .bits = { 22, 13, 30, 18 }, },
            { .regs = { REG_4, 0, REG_3, REG_3 }, .bits = { 21, 0, 29, 17 }, },
            { .regs = { REG_4, REG_3, REG_3, REG_3 }, .bits = { 20, 12, 28, 16 }, },
            { .regs = { REG_4, REG_3, REG_3, REG_3 }, .bits = { 19, 11, 27, 15 }, },
            { .regs = { REG_4, REG_3, REG_3 }, .bits = { 18, 11, 26 }, },
            { .regs = { REG_4, REG_3, REG_3, REG_4 }, .bits = { 17, 11, 25, 9 }, },
            { .regs = { REG_4, REG_3, REG_3, REG_4 }, .bits = { 16, 11, 24, 8 }, },
            { .regs = { REG_4, REG_3, 0, REG_3, REG_4 }, .bits = { 15, 11, 0, 23, 7 }, },
            { .regs = { REG_4, REG_3, 0, REG_3, REG_4 }, .bits = { 14, 11, 0, 22, 6 }, },
            { .regs = { REG_4, REG_3, 0, 0, REG_4 }, .bits = { 13, 11, 0, 0, 5 }, },
            { .regs = { REG_4, REG_3, 0, 0, REG_4 }, .bits = { 12, 11, 0, 0, 4 }, },
            { .regs = { REG_4, 0, 0, 0, REG_4 }, .bits = { 11, 0, 0, 0, 3 }, },
            { .regs = { REG_4, 0, 0, 0, REG_4 }, .bits = { 10, 0, 0, 0, 2 }, },
            { .regs = { REG_4, REG_3 }, .bits = { 25, 21 }, },
            { .regs = { REG_4, 0, REG_3 }, .bits = { 24, 0, 20 }, },
        },
    },
    // GPIOAO Block
    {
        .mux = {
            { .regs = { AO_REG, AO_REG }, .bits = { 12, 26 }, },
            { .regs = { AO_REG, AO_REG }, .bits = { 11, 25 }, },
            { .regs = { AO_REG, AO_REG }, .bits = { 10, 8 }, },
            { .regs = { AO_REG, AO_REG, 0, AO_REG }, .bits = { 9, 7, 0, 22 }, },
            { .regs = { AO_REG, AO_REG, AO_REG }, .bits = { 24, 6, 2 }, },
            { .regs = { AO_REG, AO_REG, AO_REG }, .bits = { 23, 5, 1 }, },
            { .regs = { 0, 0, AO_REG, AO_REG }, .bits = { 0, 0, 16, 18 }, },
            { .regs = { AO_REG, AO_REG }, .bits = { 0, 21 }, },
            { .regs = { AO_REG, AO_REG, AO_REG_2, AO_REG }, .bits = { 15, 14, 0, 17 }, },
            { .regs = { AO_REG, AO_REG, AO_REG_2, AO_REG }, .bits = { 31, 4, 1, 3 }, },
        },
    },
};

static_assert(countof(s912_gpio_blocks) == countof(s912_pinmux_blocks), "");

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
