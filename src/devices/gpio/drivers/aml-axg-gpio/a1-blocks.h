// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_GPIO_DRIVERS_AML_AXG_GPIO_A1_BLOCKS_H_
#define SRC_DEVICES_GPIO_DRIVERS_AML_AXG_GPIO_A1_BLOCKS_H_

#include <soc/aml-a1/a1-gpio.h>

#include "aml-axg-gpio.h"

namespace gpio {

constexpr AmlGpioBlock a1_gpio_blocks[] = {
    // GPIO P Block
    {
        .start_pin = (A1_GPIOP_START + 0),
        .pin_block = A1_GPIOP_START,
        .pin_count = 8,
        .mux_offset = A1_PERIPHS_PIN_MUX_0,
        .oen_offset = A1_PREG_PAD_GPIOP_OEN,
        .input_offset = A1_PREG_PAD_GPIOP_I,
        .output_offset = A1_PREG_PAD_GPIOP_O,
        .output_shift = 0,
        .pull_offset = A1_PREG_PAD_GPIOP_PULL_UP,
        .pull_en_offset = A1_PREG_PAD_GPIOP_PULL_EN,
        .mmio_index = 0,
        .pin_start = A1_GPIOP_PIN_START,
        .ds_offset = A1_PREG_PAD_GPIOP_DS,
    },
    {
        .start_pin = (A1_GPIOP_START + 8),
        .pin_block = A1_GPIOP_START,
        .pin_count = 5,
        .mux_offset = A1_PERIPHS_PIN_MUX_1,
        .oen_offset = A1_PREG_PAD_GPIOP_OEN,
        .input_offset = A1_PREG_PAD_GPIOP_I,
        .output_offset = A1_PREG_PAD_GPIOP_O,
        .output_shift = 0,
        .pull_offset = A1_PREG_PAD_GPIOP_PULL_UP,
        .pull_en_offset = A1_PREG_PAD_GPIOP_PULL_EN,
        .mmio_index = 0,
        .pin_start = A1_GPIOP_PIN_START,
        .ds_offset = A1_PREG_PAD_GPIOP_DS,
    },
    // GPIO B Block
    {
        .start_pin = (A1_GPIOB_START + 0),
        .pin_block = A1_GPIOB_START,
        .pin_count = 7,
        .mux_offset = A1_PERIPHS_PIN_MUX_2,
        .oen_offset = A1_PREG_PAD_GPIOB_OEN,
        .input_offset = A1_PREG_PAD_GPIOB_I,
        .output_offset = A1_PREG_PAD_GPIOB_O,
        .output_shift = 0,
        .pull_offset = A1_PREG_PAD_GPIOB_PULL_UP,
        .pull_en_offset = A1_PREG_PAD_GPIOB_PULL_EN,
        .mmio_index = 0,
        .pin_start = A1_GPIOB_PIN_START,
        .ds_offset = A1_PREG_PAD_GPIOB_DS,
    },
    // GPIO X Block
    {
        .start_pin = (A1_GPIOX_START + 0),
        .pin_block = A1_GPIOX_START,
        .pin_count = 8,
        .mux_offset = A1_PERIPHS_PIN_MUX_3,
        .oen_offset = A1_PREG_PAD_GPIOX_OEN,
        .input_offset = A1_PREG_PAD_GPIOX_I,
        .output_offset = A1_PREG_PAD_GPIOX_O,
        .output_shift = 0,
        .pull_offset = A1_PREG_PAD_GPIOX_PULL_UP,
        .pull_en_offset = A1_PREG_PAD_GPIOX_PULL_EN,
        .mmio_index = 0,
        .pin_start = A1_GPIOX_PIN_START,
        .ds_offset = A1_PREG_PAD_GPIOX_DS,
    },
    {
        .start_pin = (A1_GPIOX_START + 8),
        .pin_block = A1_GPIOX_START,
        .pin_count = 8,
        .mux_offset = A1_PERIPHS_PIN_MUX_4,
        .oen_offset = A1_PREG_PAD_GPIOX_OEN,
        .input_offset = A1_PREG_PAD_GPIOX_I,
        .output_offset = A1_PREG_PAD_GPIOX_O,
        .output_shift = 0,
        .pull_offset = A1_PREG_PAD_GPIOX_PULL_UP,
        .pull_en_offset = A1_PREG_PAD_GPIOX_PULL_EN,
        .mmio_index = 0,
        .pin_start = A1_GPIOX_PIN_START,
        .ds_offset = A1_PREG_PAD_GPIOX_DS,
    },
    {
        .start_pin = (A1_GPIOX_START + 16),
        .pin_block = A1_GPIOX_START,
        .pin_count = 1,
        .mux_offset = A1_PERIPHS_PIN_MUX_5,
        .oen_offset = A1_PREG_PAD_GPIOX_OEN,
        .input_offset = A1_PREG_PAD_GPIOX_I,
        .output_offset = A1_PREG_PAD_GPIOX_O,
        .output_shift = 0,
        .pull_offset = A1_PREG_PAD_GPIOX_PULL_UP,
        .pull_en_offset = A1_PREG_PAD_GPIOX_PULL_EN,
        .mmio_index = 0,
        .pin_start = A1_GPIOX_PIN_START,
        .ds_offset = A1_PREG_PAD_GPIOX_DS_EXT,
    },
    // GPIO F Block
    {
        .start_pin = (A1_GPIOF_START + 0),
        .pin_block = A1_GPIOF_START,
        .pin_count = 8,
        .mux_offset = A1_PERIPHS_PIN_MUX_6,
        .oen_offset = A1_PREG_PAD_GPIOF_OEN,
        .input_offset = A1_PREG_PAD_GPIOF_I,
        .output_offset = A1_PREG_PAD_GPIOF_O,
        .output_shift = 0,
        .pull_offset = A1_PREG_PAD_GPIOF_PULL_UP,
        .pull_en_offset = A1_PREG_PAD_GPIOF_PULL_EN,
        .mmio_index = 0,
        .pin_start = A1_GPIOF_PIN_START,
        .ds_offset = A1_PREG_PAD_GPIOF_DS,
    },
    {
        .start_pin = (A1_GPIOF_START + 8),
        .pin_block = A1_GPIOF_START,
        .pin_count = 5,
        .mux_offset = A1_PERIPHS_PIN_MUX_7,
        .oen_offset = A1_PREG_PAD_GPIOF_OEN,
        .input_offset = A1_PREG_PAD_GPIOF_I,
        .output_offset = A1_PREG_PAD_GPIOF_O,
        .output_shift = 0,
        .pull_offset = A1_PREG_PAD_GPIOF_PULL_UP,
        .pull_en_offset = A1_PREG_PAD_GPIOF_PULL_EN,
        .mmio_index = 0,
        .pin_start = A1_GPIOF_PIN_START,
        .ds_offset = A1_PREG_PAD_GPIOF_DS,
    },
    // GPIO A Block
    {
        .start_pin = (A1_GPIOA_START + 0),
        .pin_block = A1_GPIOA_START,
        .pin_count = 8,
        .mux_offset = A1_PERIPHS_PIN_MUX_8,
        .oen_offset = A1_PREG_PAD_GPIOA_OEN,
        .input_offset = A1_PREG_PAD_GPIOA_I,
        .output_offset = A1_PREG_PAD_GPIOA_O,
        .output_shift = 0,
        .pull_offset = A1_PREG_PAD_GPIOA_PULL_UP,
        .pull_en_offset = A1_PREG_PAD_GPIOA_PULL_EN,
        .mmio_index = 0,
        .pin_start = A1_GPIOA_PIN_START,
        .ds_offset = A1_PREG_PAD_GPIOA_DS,
    },
    {
        .start_pin = (A1_GPIOA_START + 8),
        .pin_block = A1_GPIOA_START,
        .pin_count = 4,
        .mux_offset = A1_PERIPHS_PIN_MUX_9,
        .oen_offset = A1_PREG_PAD_GPIOA_OEN,
        .input_offset = A1_PREG_PAD_GPIOA_I,
        .output_offset = A1_PREG_PAD_GPIOA_O,
        .output_shift = 0,
        .pull_offset = A1_PREG_PAD_GPIOA_PULL_UP,
        .pull_en_offset = A1_PREG_PAD_GPIOA_PULL_EN,
        .mmio_index = 0,
        .pin_start = A1_GPIOA_PIN_START,
        .ds_offset = A1_PREG_PAD_GPIOA_DS,
    },
};

constexpr AmlGpioInterrupt a1_interrupt_block = {
    .pin_select_offset = A1_GPIO_IRQ_0_1_PIN_FILTER_SELECT,
    .edge_polarity_offset = A1_GPIO_INT_EDGE_POLARITY,
    .filter_select_offset = A1_GPIO_IRQ_0_1_PIN_FILTER_SELECT,
};

}  // namespace gpio

#endif  // SRC_DEVICES_GPIO_DRIVERS_AML_AXG_GPIO_A1_BLOCKS_H_
