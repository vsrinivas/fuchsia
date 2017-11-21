// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/usb-mode-switch.h>

#include <soc/aml-a113/aml-i2c.h>

typedef struct {
    gpio_protocol_t proto;
    io_buffer_t periphs_reg;        // PMux/GPIO
    io_buffer_t periphs_ao_reg;     // PMux/GPIO for AO domain
} a113_gpio_t;

zx_status_t a113_pinmux_config(a113_gpio_t* gpio, const uint32_t pin, const uint32_t fn);
zx_status_t a113_gpio_init(a113_gpio_t* gpio);
void a113_gpio_release(a113_gpio_t* gpio);
