// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/usb-mode-switch.h>
#include <soc/hi3660/hi3660.h>

// BTI IDs for our devices
enum {
    BTI_BOARD,
    BTI_USB_DWC3,
    BTI_DSI,
    BTI_MALI,
};

typedef struct {
    platform_bus_protocol_t pbus;
    gpio_protocol_t gpio;
    zx_device_t* parent;
    zx_handle_t bti_handle;
    hi3660_t* hi3660;
    usb_mode_t usb_mode;
} hikey960_t;

// hikey960-devices.c
zx_status_t hikey960_add_devices(hikey960_t* bus);

// hikey960-i2c.c
zx_status_t hikey960_i2c_init(hikey960_t* bus);
