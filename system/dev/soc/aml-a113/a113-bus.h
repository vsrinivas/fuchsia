// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/usb-mode-switch.h>

typedef struct {
    platform_bus_protocol_t pbus;
    usb_mode_switch_protocol_t usb_mode_switch;
    gpio_protocol_t gpio;
    io_buffer_t usb_phy;
    io_buffer_t periphs_reg;        // PMux/GPIO
    io_buffer_t periphs_ao_reg;     // PMux/GPIO for AO domain
    // more coming soon

} a113_bus_t;

// a113-audio.c
zx_status_t a113_audio_init(a113_bus_t* bus);

// a113-pinmux.c
zx_status_t a113_pinmux_config(void* ctx, const uint32_t pin, const uint32_t fn);
zx_status_t a113_gpio_init(a113_bus_t* bus);
void a113_gpio_release(a113_bus_t* bus);

// a113-usb.c
zx_status_t a113_usb_init(a113_bus_t* bus);
zx_status_t a113_usb_set_mode(a113_bus_t* bus, usb_mode_t mode);
