// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-bus.h>
#include <zircon/device/usb-device.h>
#include <ddk/protocol/usb-mode-switch.h>
#include <zircon/listnode.h>

typedef struct {
    list_node_t gpios;
    platform_bus_protocol_t pbus;
    gpio_protocol_t gpio;
    usb_mode_switch_protocol_t usb_mode_switch;
    io_buffer_t usb3otg_bc;
    io_buffer_t peri_crg;
    io_buffer_t pctrl;
    usb_mode_t usb_mode;
} hi3660_bus_t;

// hi3660-devices.c
zx_status_t hi3360_add_devices(hi3660_bus_t* bus);

// hi3660-gpios.c
zx_status_t hi3360_add_gpios(hi3660_bus_t* bus);

// hi3660-usb.c
zx_status_t hi3360_usb_init(hi3660_bus_t* bus);
zx_status_t hi3660_usb_set_mode(hi3660_bus_t* bus, usb_mode_t mode);
