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
    gpio_protocol_t gpio;
    i2c_protocol_t i2c;
    io_buffer_t periphs_reg;        // PMux/GPIO
    io_buffer_t periphs_ao_reg;     // PMux/GPIO for AO domain
    aml_i2c_dev_t* i2c_devs[AML_I2C_COUNT];
} a113_bus_t;

zx_status_t a113_bus_init(a113_bus_t** out);
void a113_bus_release(a113_bus_t* bus);

// a113-gpio.c
zx_status_t a113_pinmux_config(void* ctx, const uint32_t pin, const uint32_t fn);
zx_status_t a113_gpio_init(a113_bus_t* bus);
void a113_gpio_release(a113_bus_t* bus);

// a113-i2c.c
zx_status_t a113_i2c_init(a113_bus_t* bus);
