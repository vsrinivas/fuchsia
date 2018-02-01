// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c.h>

typedef struct {
    platform_bus_protocol_t pbus;
    gpio_protocol_t gpio;
    i2c_protocol_t i2c;
    zx_device_t* parent;
    io_buffer_t usb_phy;
    uint32_t soc_pid;
} vim_bus_t;

// vim-gpio.c
zx_status_t vim_gpio_init(vim_bus_t* bus);

// vim-i2c.c
zx_status_t vim_i2c_init(vim_bus_t* bus);

// vim-usb.c
zx_status_t vim_usb_init(vim_bus_t* bus);
