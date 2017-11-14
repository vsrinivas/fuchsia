// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/usb-mode-switch.h>
#include <soc/aml-a113/a113-bus.h>

typedef struct {
    platform_bus_protocol_t pbus;
    a113_bus_t* a113;
    usb_mode_switch_protocol_t usb_mode_switch;
    io_buffer_t usb_phy;
} gauss_bus_t;

// gauss-audio.c
zx_status_t gauss_audio_init(gauss_bus_t* bus);

// gauss-usb.c
zx_status_t gauss_usb_init(gauss_bus_t* bus);
zx_status_t gauss_usb_set_mode(gauss_bus_t* bus, usb_mode_t mode);
