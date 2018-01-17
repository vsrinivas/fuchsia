// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <soc/aml-a113/a113-hw.h>

#include <limits.h>

#include "gauss.h"

#define PAGE_MASK (PAGE_SIZE - 1)

// Physical Base Address for Pinmux/GPIO Control
#define GPIO_BASE_PHYS 0xff634400
#define GPIO_BASE_PAGE ((~PAGE_MASK) & GPIO_BASE_PHYS)

// The GPIO "Always On" Domain has its own control block mapped here.
#define GPIOAO_BASE_PHYS 0xff800000
#define GPIOAO_BASE_PAGE ((~PAGE_MASK) & GPIOAO_BASE_PHYS)

static aml_gpio_block_t gpio_blocks[] = {
    // GPIO X Block
    {
        .start_pin = (A113_GPIOX_START + 0),
        .pin_block = A113_GPIOX_START,
        .pin_count = 8,
        .mux_offset = PERIPHS_PIN_MUX_4,
        .ctrl_offset = GPIO_REG2_EN_N,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },
    {
        .start_pin = (A113_GPIOX_START + 8),
        .pin_block = A113_GPIOX_START,
        .pin_count = 8,
        .mux_offset = PERIPHS_PIN_MUX_5,
        .ctrl_offset = GPIO_REG2_EN_N,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },
    {
        .start_pin = (A113_GPIOX_START + 16),
        .pin_block = A113_GPIOX_START,
        .pin_count = 7,
        .mux_offset = PERIPHS_PIN_MUX_6,
        .ctrl_offset = GPIO_REG2_EN_N,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },

    // GPIO A Block
    {
        .start_pin = (A113_GPIOA_START + 0),
        .pin_block = A113_GPIOA_START,
        .pin_count = 8,
        .mux_offset = PERIPHS_PIN_MUX_B,
        .ctrl_offset = GPIO_REG0_EN_N,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },
    {
        .start_pin = (A113_GPIOA_START + 8),
        .pin_block = A113_GPIOA_START,
        .pin_count = 8,
        .mux_offset = PERIPHS_PIN_MUX_C,
        .ctrl_offset = GPIO_REG0_EN_N,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },
    {
        .start_pin = (A113_GPIOA_START + 16),
        .pin_block = A113_GPIOA_START,
        .pin_count = 5,
        .mux_offset = PERIPHS_PIN_MUX_D,
        .ctrl_offset = GPIO_REG0_EN_N,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },

    // GPIO Boot Block
    {
        .start_pin = (A113_GPIOB_START + 0),
        .pin_block = A113_GPIOB_START,
        .pin_count = 8,
        .mux_offset = PERIPHS_PIN_MUX_0,
        .ctrl_offset = GPIO_REG4_EN_N,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },
    {
        .start_pin = (A113_GPIOB_START + 8),
        .pin_block = A113_GPIOB_START,
        .pin_count = 7,
        .mux_offset = PERIPHS_PIN_MUX_1,
        .ctrl_offset = GPIO_REG4_EN_N,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },

    // GPIO Y Block
    {
        .start_pin = (A113_GPIOY_START + 0),
        .pin_block = A113_GPIOY_START,
        .pin_count = 8,
        .mux_offset = PERIPHS_PIN_MUX_8,
        .ctrl_offset = GPIO_REG1_EN_N,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },
    {
        .start_pin = (A113_GPIOY_START + 8),
        .pin_block = A113_GPIOY_START,
        .pin_count = 8,
        .mux_offset = PERIPHS_PIN_MUX_9,
        .ctrl_offset = GPIO_REG1_EN_N,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },

    // GPIO Z Block
    {
        .start_pin = (A113_GPIOZ_START + 0),
        .pin_block = A113_GPIOZ_START,
        .pin_count = 8,
        .mux_offset = PERIPHS_PIN_MUX_2,
        .ctrl_offset = GPIO_REG3_EN_N,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },
    {
        .start_pin = (A113_GPIOZ_START + 8),
        .pin_block = A113_GPIOZ_START,
        .pin_count = 3,
        .mux_offset = PERIPHS_PIN_MUX_3,
        .ctrl_offset = GPIO_REG3_EN_N,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },

    // GPIO AO Block
    // NOTE: The GPIO AO block has a seperate control block than the other
    //       GPIO blocks.
    {
        .start_pin = (A113_GPIOAO_START + 0),
        .pin_block = A113_GPIOAO_START,
        .pin_count = 8,
        .mux_offset = AO_RTI_PIN_MUX_REG0,
        .ctrl_offset = AO_GPIO_O_EN_N,
        .ctrl_block_base_phys = GPIOAO_BASE_PAGE,
        .lock = MTX_INIT,
    },
    {
        .start_pin = (A113_GPIOAO_START + 8),
        .pin_block = A113_GPIOAO_START,
        .pin_count = 6,
        .mux_offset = AO_RTI_PIN_MUX_REG1,
        .ctrl_offset = AO_GPIO_O_EN_N,
        .ctrl_block_base_phys = GPIOAO_BASE_PAGE,
        .lock = MTX_INIT,
    },
};

zx_status_t gauss_gpio_init(aml_gpio_t* gpio) {
    return aml_gpio_init(gpio, GPIO_BASE_PHYS, GPIOAO_BASE_PHYS, gpio_blocks,
                         countof(gpio_blocks));
}
