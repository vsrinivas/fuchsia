// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/usb-mode-switch.h>
#include <soc/hi3660/hi3660.h>

typedef struct {
    platform_bus_protocol_t pbus;
    hi3660_t* hi3660;
    usb_mode_switch_protocol_t usb_mode_switch;
    usb_mode_t usb_mode;
} hikey960_t;

// hikey960-devices.c
zx_status_t hikey960_add_devices(hikey960_t* bus);
