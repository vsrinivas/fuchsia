// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/usb-mode-switch.h>
#include <soc/aml-a113/a113-clocks.h>

#include <threads.h>

typedef struct {
    platform_bus_protocol_t pbus;
    zx_device_t* parent;
    gpio_protocol_t gpio;
    i2c_protocol_t i2c;
    usb_mode_switch_protocol_t usb_mode_switch;
    io_buffer_t usb_phy;
    zx_handle_t usb_phy_irq_handle;
    thrd_t phy_irq_thread;
    a113_clk_dev_t *clocks;
} gauss_bus_t;

// gauss-audio.c
zx_status_t gauss_audio_init(gauss_bus_t* bus);

// gauss-gpio.c
zx_status_t gauss_gpio_init(gauss_bus_t* bus);

// gauss-i2c.c
zx_status_t gauss_i2c_init(gauss_bus_t* bus);

// gauss-usb.c
zx_status_t gauss_usb_init(gauss_bus_t* bus);
zx_status_t gauss_usb_set_mode(gauss_bus_t* bus, usb_mode_t mode);
