// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-bus.h>
//#include <soc/aml-a113/a113-gpio.h>
//#include <soc/aml-a113/a113-i2c.h>

typedef struct {
    platform_bus_protocol_t pbus;
//    a113_gpio_t gpio;
//    a113_i2c_t i2c;
    io_buffer_t usb_phy;
} vim_bus_t;

// vim-usb.c
zx_status_t vim_usb_init(vim_bus_t* bus);
