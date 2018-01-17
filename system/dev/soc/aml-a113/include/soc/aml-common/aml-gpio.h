// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/usb-mode-switch.h>

#include <threads.h>

typedef struct {
    uint32_t start_pin;
    uint32_t pin_block;
    uint32_t pin_count;
    uint32_t mux_offset;
    uint32_t ctrl_offset;
    zx_paddr_t ctrl_block_base_phys;
    zx_vaddr_t ctrl_block_base_virt;
    mtx_t lock;
} aml_gpio_block_t;

typedef struct {
    gpio_protocol_t proto;
    io_buffer_t periphs_reg;        // PMux/GPIO
    io_buffer_t periphs_ao_reg;     // PMux/GPIO for AO domain
    aml_gpio_block_t* gpio_blocks;
    size_t gpio_block_count;
} aml_gpio_t;

zx_status_t aml_pinmux_config(aml_gpio_t* gpio, const uint32_t pin, const uint32_t fn);
zx_status_t aml_gpio_init(aml_gpio_t* gpio, zx_paddr_t gpio_base, zx_paddr_t a0_base,
                          aml_gpio_block_t* gpio_blocks, size_t gpio_block_count);
void aml_gpio_release(aml_gpio_t* gpio);
